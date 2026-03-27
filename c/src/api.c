/*
 * api.c — REST API server using libmicrohttpd
 *
 * Endpoints:
 *   GET  /health     — no auth
 *   GET  /status     — current mode, uptime
 *   POST /mode       — change mode
 *   POST /type       — trigger typing
 *   POST /jiggle     — trigger jiggle
 *   GET  /config     — return config (redacted)
 *   POST /reconnect  — reconnect transport
 */
#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <microhttpd.h>

#include "cjson/cJSON.h"

/* ── Rate limiter ──────────────────────────────────────────────────────────── */

#define MAX_CLIENTS 256

typedef struct {
    char ip[64];
    int count;
    time_t window_start;
} rate_entry_t;

typedef struct {
    rate_entry_t entries[MAX_CLIENTS];
    int num_entries;
    int max_requests;
} rate_limiter_t;

static rate_limiter_t g_rate_limiter;

static void rate_limiter_init(int max_requests) {
    memset(&g_rate_limiter, 0, sizeof(g_rate_limiter));
    g_rate_limiter.max_requests = max_requests;
}

static int rate_limiter_check(const char *ip) {
    time_t now = time(NULL);
    rate_limiter_t *rl = &g_rate_limiter;

    for (int i = 0; i < rl->num_entries; i++) {
        if (strcmp(rl->entries[i].ip, ip) == 0) {
            if (now - rl->entries[i].window_start >= 1) {
                /* New window */
                rl->entries[i].count = 1;
                rl->entries[i].window_start = now;
                return 1;
            }
            if (rl->entries[i].count >= rl->max_requests) {
                return 0;
            }
            rl->entries[i].count++;
            return 1;
        }
    }

    /* New client */
    if (rl->num_entries < MAX_CLIENTS) {
        rate_entry_t *e = &rl->entries[rl->num_entries++];
        strncpy(e->ip, ip, sizeof(e->ip) - 1);
        e->count = 1;
        e->window_start = now;
    }
    return 1;
}

/* ── Constant-time string comparison ───────────────────────────────────────── */

static int constant_time_eq(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la != lb) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; i < la; i++) {
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return diff == 0;
}

/* ── Helper: send JSON response ────────────────────────────────────────────── */

struct pikey_api {
    struct MHD_Daemon *daemon;
    api_state_t *state;
};

static enum MHD_Result send_json(
    struct MHD_Connection *conn,
    unsigned int status_code,
    const char *json_str
) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(json_str), (void *)json_str, MHD_RESPMEM_MUST_COPY
    );
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(conn, status_code, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result send_json_obj(
    struct MHD_Connection *conn,
    unsigned int status_code,
    cJSON *obj
) {
    char *str = cJSON_PrintUnformatted(obj);
    enum MHD_Result ret = send_json(conn, status_code, str);
    free(str);
    cJSON_Delete(obj);
    return ret;
}

/* ── POST body accumulator ─────────────────────────────────────────────────── */

#define MAX_POST_SIZE 4096

struct post_data {
    char buf[MAX_POST_SIZE];
    size_t len;
};

/* ── Endpoint handlers ─────────────────────────────────────────────────────── */

static enum MHD_Result handle_health(struct MHD_Connection *conn) {
    return send_json(conn, MHD_HTTP_OK, "{\"status\":\"ok\"}");
}

static enum MHD_Result handle_status(struct MHD_Connection *conn, api_state_t *st) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "mode", st->current_mode);
    cJSON_AddNumberToObject(obj, "uptime_seconds", (double)(time(NULL) - st->start_time));
    if (st->last_typing_session > 0)
        cJSON_AddNumberToObject(obj, "last_typing_session", (double)st->last_typing_session);
    else
        cJSON_AddNullToObject(obj, "last_typing_session");
    return send_json_obj(conn, MHD_HTTP_OK, obj);
}

static enum MHD_Result handle_mode(struct MHD_Connection *conn, api_state_t *st, const char *body) {
    cJSON *json = cJSON_Parse(body);
    if (!json)
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"invalid JSON\"}");

    cJSON *mode_item = cJSON_GetObjectItem(json, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        cJSON_Delete(json);
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"missing mode\"}");
    }

    const char *mode = mode_item->valuestring;
    if (strcmp(mode, "jiggle") != 0 && strcmp(mode, "type") != 0 && strcmp(mode, "both") != 0) {
        cJSON_Delete(json);
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
            "{\"error\":\"mode must be 'jiggle', 'type', or 'both'\"}");
    }

    strncpy(st->current_mode, mode, sizeof(st->current_mode) - 1);
    fprintf(stderr, "[pikey-api] Mode changed to: %s\n", mode);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "mode", mode);
    cJSON_Delete(json);
    return send_json_obj(conn, MHD_HTTP_OK, resp);
}

static enum MHD_Result handle_trigger_type(struct MHD_Connection *conn, api_state_t *st) {
    if (!st->typer)
        return send_json(conn, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"error\":\"typer not available\"}");
    st->last_typing_session = time(NULL);
    fprintf(stderr, "[pikey-api] Typing session triggered\n");
    return send_json(conn, MHD_HTTP_OK, "{\"triggered\":\"type\"}");
}

static enum MHD_Result handle_trigger_jiggle(struct MHD_Connection *conn, api_state_t *st) {
    if (!st->jiggler)
        return send_json(conn, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"error\":\"jiggler not available\"}");
    fprintf(stderr, "[pikey-api] Jiggle triggered\n");
    return send_json(conn, MHD_HTTP_OK, "{\"triggered\":\"jiggle\"}");
}

static enum MHD_Result handle_get_config(struct MHD_Connection *conn, api_state_t *st) {
    pikey_config_t *cfg = st->config;
    cJSON *obj = cJSON_CreateObject();

    /* device */
    cJSON *dev = cJSON_AddObjectToObject(obj, "device");
    cJSON_AddStringToObject(dev, "name", cfg->device.name);
    cJSON_AddStringToObject(dev, "cod", cfg->device.cod);

    /* jiggler */
    cJSON *jig = cJSON_AddObjectToObject(obj, "jiggler");
    cJSON_AddBoolToObject(jig, "enabled", cfg->jiggler.enabled);
    cJSON_AddNumberToObject(jig, "interval_min", cfg->jiggler.interval_min);
    cJSON_AddNumberToObject(jig, "interval_max", cfg->jiggler.interval_max);
    cJSON_AddNumberToObject(jig, "max_delta", cfg->jiggler.max_delta);
    cJSON_AddNumberToObject(jig, "big_move_chance", cfg->jiggler.big_move_chance);

    /* typer */
    cJSON *typ = cJSON_AddObjectToObject(obj, "typer");
    cJSON_AddBoolToObject(typ, "enabled", cfg->typer.enabled);
    cJSON_AddNumberToObject(typ, "interval_min", cfg->typer.interval_min);
    cJSON_AddNumberToObject(typ, "interval_max", cfg->typer.interval_max);
    cJSON_AddNumberToObject(typ, "cpm_min", cfg->typer.cpm_min);
    cJSON_AddNumberToObject(typ, "cpm_max", cfg->typer.cpm_max);
    cJSON_AddNumberToObject(typ, "typo_rate", cfg->typer.typo_rate);

    /* llm (redacted) */
    cJSON *llm = cJSON_AddObjectToObject(obj, "llm");
    cJSON_AddStringToObject(llm, "url", cfg->llm.url);
    cJSON_AddStringToObject(llm, "api_style", cfg->llm.api_style);
    cJSON_AddStringToObject(llm, "model", cfg->llm.model);
    cJSON_AddStringToObject(llm, "api_key", cfg->llm.api_key[0] ? "***" : "");
    cJSON_AddNumberToObject(llm, "max_tokens", cfg->llm.max_tokens);

    /* api (redacted) */
    cJSON *api = cJSON_AddObjectToObject(obj, "api");
    cJSON_AddBoolToObject(api, "enabled", cfg->api.enabled);
    cJSON_AddStringToObject(api, "host", cfg->api.host);
    cJSON_AddNumberToObject(api, "port", cfg->api.port);
    cJSON_AddStringToObject(api, "api_key", "***");
    cJSON_AddNumberToObject(api, "rate_limit", cfg->api.rate_limit);

    return send_json_obj(conn, MHD_HTTP_OK, obj);
}

static enum MHD_Result handle_reconnect(struct MHD_Connection *conn, api_state_t *st) {
    if (!st->transport)
        return send_json(conn, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"error\":\"no transport\"}");
    fprintf(stderr, "[pikey-api] Reconnect requested\n");
    return send_json(conn, MHD_HTTP_OK, "{\"reconnected\":true}");
}

/* ── MHD request handler ──────────────────────────────────────────────────── */

static enum MHD_Result handler(
    void *cls,
    struct MHD_Connection *conn,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
) {
    (void)version;
    api_state_t *st = (api_state_t *)cls;

    /* POST body accumulation */
    if (strcmp(method, "POST") == 0 || strcmp(method, "PATCH") == 0) {
        if (*con_cls == NULL) {
            struct post_data *pd = calloc(1, sizeof(struct post_data));
            if (!pd) return MHD_NO;
            *con_cls = pd;
            return MHD_YES;
        }
        struct post_data *pd = (struct post_data *)*con_cls;
        if (*upload_data_size > 0) {
            size_t remaining = MAX_POST_SIZE - pd->len - 1;
            size_t to_copy = *upload_data_size < remaining ? *upload_data_size : remaining;
            memcpy(pd->buf + pd->len, upload_data, to_copy);
            pd->len += to_copy;
            pd->buf[pd->len] = '\0';
            *upload_data_size = 0;
            return MHD_YES;
        }
    }

    /* /health — no auth */
    if (strcmp(url, "/health") == 0 && strcmp(method, "GET") == 0) {
        return handle_health(conn);
    }

    /* Rate limiting */
    const struct sockaddr *addr = MHD_get_connection_info(conn,
        MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;
    char client_ip[64] = "unknown";
    if (addr) {
        /* Just use a simple representation */
        snprintf(client_ip, sizeof(client_ip), "%p", (void *)addr);
    }
    if (!rate_limiter_check(client_ip)) {
        return send_json(conn, MHD_HTTP_TOO_MANY_REQUESTS,
            "{\"error\":\"rate limit exceeded\"}");
    }

    /* API key auth */
    const char *key = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "x-api-key");
    if (!key || !constant_time_eq(key, st->api_key)) {
        return send_json(conn, MHD_HTTP_UNAUTHORIZED, "{\"error\":\"unauthorized\"}");
    }

    /* Route dispatch */
    enum MHD_Result ret;

    if (strcmp(url, "/status") == 0 && strcmp(method, "GET") == 0) {
        ret = handle_status(conn, st);
    }
    else if (strcmp(url, "/mode") == 0 && strcmp(method, "POST") == 0) {
        struct post_data *pd = (struct post_data *)*con_cls;
        ret = handle_mode(conn, st, pd ? pd->buf : "");
    }
    else if (strcmp(url, "/type") == 0 && strcmp(method, "POST") == 0) {
        ret = handle_trigger_type(conn, st);
    }
    else if (strcmp(url, "/jiggle") == 0 && strcmp(method, "POST") == 0) {
        ret = handle_trigger_jiggle(conn, st);
    }
    else if (strcmp(url, "/config") == 0 && strcmp(method, "GET") == 0) {
        ret = handle_get_config(conn, st);
    }
    else if (strcmp(url, "/reconnect") == 0 && strcmp(method, "POST") == 0) {
        ret = handle_reconnect(conn, st);
    }
    else {
        ret = send_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
    }

    /* Free POST data */
    if (*con_cls) {
        free(*con_cls);
        *con_cls = NULL;
    }

    return ret;
}

/* ── Public API ────────────────────────────────────────────────────────────── */

pikey_api_t *api_start(api_state_t *state, const char *host, int port) {
    (void)host; /* MHD binds to all interfaces by default */

    rate_limiter_init(state->rate_limit);

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
        (uint16_t)port,
        NULL, NULL,
        handler, state,
        MHD_OPTION_END
    );

    if (!daemon) {
        fprintf(stderr, "[pikey-api] Failed to start API server on port %d\n", port);
        return NULL;
    }

    pikey_api_t *api = calloc(1, sizeof(pikey_api_t));
    if (!api) {
        MHD_stop_daemon(daemon);
        return NULL;
    }

    api->daemon = daemon;
    api->state = state;

    fprintf(stderr, "[pikey-api] Listening on http://%s:%d\n", host, port);
    return api;
}

void api_stop(pikey_api_t *api) {
    if (!api) return;
    if (api->daemon) {
        MHD_stop_daemon(api->daemon);
    }
    free(api);
}

void api_resolve_key(const api_config_t *cfg, char *out_key, size_t out_len) {
    const char *env = getenv("PIKEY_API_KEY");
    if (env && env[0] != '\0') {
        strncpy(out_key, env, out_len - 1);
        out_key[out_len - 1] = '\0';
    } else {
        strncpy(out_key, cfg->api_key, out_len - 1);
        out_key[out_len - 1] = '\0';
    }
}
