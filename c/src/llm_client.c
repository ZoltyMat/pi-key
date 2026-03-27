/*
 * llm_client.c — HTTP client for LLM APIs using libcurl
 */
#include "llm_client.h"
#include "cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* ── Response buffer for curl ──────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} response_buf_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buf_t *buf = (response_buf_t *)userp;

    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "[llm] realloc failed\n");
        return 0;
    }

    buf->data = ptr;
    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void llm_client_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void llm_client_cleanup(void) {
    curl_global_cleanup();
}

char *llm_fetch_text(const char *prompt, const llm_config_t *cfg) {
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    response_buf_t resp = { .data = NULL, .size = 0 };
    char *result = NULL;
    char *json_str = NULL;
    char url[512];
    int is_ollama;

    if (!cfg->url[0]) {
        fprintf(stderr, "[llm] No URL configured\n");
        return NULL;
    }

    is_ollama = (strcmp(cfg->api_style, "ollama") == 0);

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[llm] curl_easy_init failed\n");
        return NULL;
    }

    /* Build URL */
    if (is_ollama) {
        snprintf(url, sizeof(url), "%s/api/generate", cfg->url);
    } else {
        snprintf(url, sizeof(url), "%s/v1/chat/completions", cfg->url);
    }

    /* Remove trailing slash from base URL if present */
    {
        size_t base_len = strlen(cfg->url);
        if (base_len > 0 && cfg->url[base_len - 1] == '/') {
            /* Rebuild URL without trailing slash */
            char base[MAX_STR_LEN];
            strncpy(base, cfg->url, MAX_STR_LEN - 1);
            base[base_len - 1] = '\0';
            if (is_ollama) {
                snprintf(url, sizeof(url), "%s/api/generate", base);
            } else {
                snprintf(url, sizeof(url), "%s/v1/chat/completions", base);
            }
        }
    }

    /* Build JSON payload */
    cJSON *payload = cJSON_CreateObject();
    if (!payload) goto cleanup;

    if (is_ollama) {
        cJSON_AddStringToObject(payload, "prompt", prompt);
        cJSON_AddBoolToObject(payload, "stream", 0);

        cJSON *options = cJSON_CreateObject();
        cJSON_AddNumberToObject(options, "num_predict", cfg->max_tokens);
        cJSON_AddItemToObject(payload, "options", options);

        if (cfg->model[0]) {
            cJSON_AddStringToObject(payload, "model", cfg->model);
        }
    } else {
        /* OpenAI-compatible */
        cJSON *messages = cJSON_CreateArray();
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "user");
        cJSON_AddStringToObject(msg, "content", prompt);
        cJSON_AddItemToArray(messages, msg);
        cJSON_AddItemToObject(payload, "messages", messages);

        cJSON_AddNumberToObject(payload, "max_tokens", cfg->max_tokens);
        cJSON_AddBoolToObject(payload, "stream", 0);

        if (cfg->model[0]) {
            cJSON_AddStringToObject(payload, "model", cfg->model);
        }
    }

    json_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    payload = NULL;

    if (!json_str) goto cleanup;

    /* Set up headers */
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (cfg->api_key[0]) {
        char auth[MAX_STR_LEN + 32];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);
        headers = curl_slist_append(headers, auth);
    }

    /* Configure curl */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    /* Perform request */
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[llm] curl error: %s\n", curl_easy_strerror(res));
        goto cleanup;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        fprintf(stderr, "[llm] HTTP %ld: %s\n", http_code, resp.data ? resp.data : "(empty)");
        goto cleanup;
    }

    /* Parse response JSON */
    if (!resp.data) goto cleanup;

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        fprintf(stderr, "[llm] Failed to parse JSON response\n");
        goto cleanup;
    }

    if (is_ollama) {
        cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
        if (cJSON_IsString(response) && response->valuestring) {
            result = strdup(response->valuestring);
        }
    } else {
        /* OpenAI: choices[0].message.content */
        cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
        if (cJSON_IsArray(choices)) {
            cJSON *first = cJSON_GetArrayItem(choices, 0);
            if (first) {
                cJSON *message = cJSON_GetObjectItemCaseSensitive(first, "message");
                if (message) {
                    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
                    if (cJSON_IsString(content) && content->valuestring) {
                        result = strdup(content->valuestring);
                    }
                }
            }
        }
    }

    cJSON_Delete(root);

    /* Trim leading/trailing whitespace */
    if (result) {
        char *start = result;
        while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t') start++;
        char *end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) end--;
        *(end + 1) = '\0';

        if (start != result) {
            char *trimmed = strdup(start);
            free(result);
            result = trimmed;
        }
    }

cleanup:
    if (json_str) free(json_str);
    if (resp.data) free(resp.data);
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    if (payload) cJSON_Delete(payload);
    return result;
}
