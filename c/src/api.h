/*
 * api.h — REST API server for PiKey remote control
 *
 * Lightweight HTTP server using libmicrohttpd.
 * Provides /health, /status, /mode, /type, /jiggle, /config, /reconnect.
 * API key auth on all endpoints except /health.
 * Rate limiting per client IP.
 */
#ifndef PIKEY_API_H
#define PIKEY_API_H

#include <stdbool.h>
#include <time.h>
#include "config.h"
#include "transport.h"
#include "jiggler.h"
#include "typer.h"

typedef struct pikey_api pikey_api_t;

/*
 * Shared state accessible to API handlers.
 */
typedef struct {
    pikey_config_t *config;
    char current_mode[16];
    time_t start_time;
    time_t last_typing_session;
    hid_transport_t *transport;
    jiggler_t *jiggler;
    typer_t *typer;
    char api_key[256];
    int rate_limit;
} api_state_t;

/*
 * Create and start the API server.
 * Returns NULL on failure. Caller must stop + destroy with api_stop/api_destroy.
 */
pikey_api_t *api_start(api_state_t *state, const char *host, int port);

/*
 * Stop the API server and free resources.
 */
void api_stop(pikey_api_t *api);

/*
 * Resolve API key from config or PIKEY_API_KEY env var.
 * Writes result into out_key (max out_len bytes).
 */
void api_resolve_key(const api_config_t *cfg, char *out_key, size_t out_len);

#endif /* PIKEY_API_H */
