/*
 * main.c — PiKey entrypoint
 *
 * Bluetooth HID spoofer + LLM auto-typer for Raspberry Pi.
 * Presents as a Logitech K380 keyboard+mouse combo.
 *
 * Usage:
 *   pikey --mode jiggle              # mouse only
 *   pikey --mode type                # LLM typing only
 *   pikey --mode both                # jiggle + typing (default)
 *   pikey --transport usb            # USB gadget mode
 *   pikey --transport bt             # Bluetooth (default)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>

#include "config.h"
#include "transport.h"
#include "jiggler.h"
#include "typer.h"
#include "llm_client.h"
#include "api.h"

/* ── Globals for signal handler cleanup ────────────────────────────────────── */

static jiggler_t *g_jiggler = NULL;
static typer_t *g_typer = NULL;
static hid_transport_t *g_transport = NULL;
static pikey_api_t *g_api = NULL;
static volatile int g_running = 1;

typedef enum {
    MODE_JIGGLE,
    MODE_TYPE,
    MODE_BOTH,
} pikey_mode_t;

typedef enum {
    TRANSPORT_BT,
    TRANSPORT_USB,
} transport_type_t;

static void shutdown_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\n[pikey] Shutting down...\n");
    g_running = 0;

    if (g_jiggler) jiggler_stop(g_jiggler);
    if (g_typer) typer_stop(g_typer);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "PiKey — BT HID Spoofer + LLM Auto-Typer\n\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  --mode MODE        Operating mode: jiggle, type, both (default: both)\n"
        "  --config PATH      Path to config.yaml (default: config.yaml)\n"
        "  --transport TYPE   Transport: bt, usb (default: bt)\n"
        "  --api              Enable REST API server\n"
        "  --help             Show this help\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *config_path = "config.yaml";
    pikey_mode_t mode = MODE_BOTH;
    transport_type_t transport_type = TRANSPORT_BT;
    int enable_api = 0;
    pikey_config_t cfg;

    /* Parse arguments */
    static struct option long_options[] = {
        { "mode",      required_argument, NULL, 'm' },
        { "config",    required_argument, NULL, 'c' },
        { "transport", required_argument, NULL, 't' },
        { "api",       no_argument,       NULL, 'a' },
        { "help",      no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:c:t:ah", long_options, NULL)) != -1) {
        switch (opt) {
        case 'm':
            if (strcmp(optarg, "jiggle") == 0) mode = MODE_JIGGLE;
            else if (strcmp(optarg, "type") == 0) mode = MODE_TYPE;
            else if (strcmp(optarg, "both") == 0) mode = MODE_BOTH;
            else {
                fprintf(stderr, "Unknown mode: %s (use jiggle, type, or both)\n", optarg);
                return 1;
            }
            break;
        case 'c':
            config_path = optarg;
            break;
        case 't':
            if (strcmp(optarg, "bt") == 0) transport_type = TRANSPORT_BT;
            else if (strcmp(optarg, "usb") == 0) transport_type = TRANSPORT_USB;
            else {
                fprintf(stderr, "Unknown transport: %s (use bt or usb)\n", optarg);
                return 1;
            }
            break;
        case 'a':
            enable_api = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Seed RNG */
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    /* Load config */
    if (parse_config(config_path, &cfg) != 0) {
        return 1;
    }

    /* Validate LLM config if typing is enabled */
    if ((mode == MODE_TYPE || mode == MODE_BOTH) && cfg.typer.enabled) {
        if (cfg.llm.url[0] == '\0') {
            fprintf(stderr,
                "[pikey] Error: llm.url is not set in %s\n"
                "  Set it to your LiteLLM/Ollama/OpenAI-compatible endpoint.\n"
                "  Or run with --mode jiggle to use only the mouse jiggler.\n",
                config_path);
            return 1;
        }
    }

    /* Banner */
    fprintf(stderr, "════════════════════════════════════════════════\n");
    fprintf(stderr, "  PiKey — BT HID Spoofer (C implementation)\n");
    fprintf(stderr, "════════════════════════════════════════════════\n");
    fprintf(stderr, "[pikey] Mode: %s\n",
            mode == MODE_JIGGLE ? "jiggle" :
            mode == MODE_TYPE ? "type" : "both");
    fprintf(stderr, "[pikey] Transport: %s\n",
            transport_type == TRANSPORT_BT ? "bluetooth" : "usb");
    fprintf(stderr, "[pikey] Spoofing as: %s\n", cfg.device.name);

    /* Create transport */
    if (transport_type == TRANSPORT_BT) {
        g_transport = bt_transport_create();
    } else {
        g_transport = usb_transport_create();
    }

    if (!g_transport) {
        fprintf(stderr, "[pikey] Failed to create transport\n");
        return 1;
    }

    /* Connect */
    const char *target = cfg.device.target_mac;
    if (target[0] == '\0') target = NULL;

    if (transport_type == TRANSPORT_BT) {
        if (target) {
            fprintf(stderr, "[pikey] Connecting to paired host: %s\n", target);
        } else {
            fprintf(stderr, "[pikey] Waiting for connection... pair this device on your target\n");
        }
    }

    if (g_transport->connect(g_transport, target) != 0) {
        fprintf(stderr, "[pikey] Failed to connect transport\n");
        if (transport_type == TRANSPORT_BT) {
            bt_transport_destroy(g_transport);
        } else {
            usb_transport_destroy(g_transport);
        }
        return 1;
    }

    /* Initialize LLM client */
    llm_client_init();

    /* Install signal handlers */
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

    /* Start workers */
    if (mode == MODE_JIGGLE || mode == MODE_BOTH) {
        g_jiggler = jiggler_create(g_transport, &cfg.jiggler);
        if (g_jiggler) {
            jiggler_start(g_jiggler);
        }
    }

    if (mode == MODE_TYPE || mode == MODE_BOTH) {
        g_typer = typer_create(g_transport, &cfg.typer, &cfg.llm);
        if (g_typer) {
            typer_start(g_typer);
        }
    }

    /* Start API server if enabled */
    if (enable_api || cfg.api.enabled) {
        char resolved_key[256];
        api_resolve_key(&cfg.api, resolved_key, sizeof(resolved_key));
        if (resolved_key[0] == '\0') {
            fprintf(stderr, "[pikey] Error: api.api_key is required when API is enabled\n");
            goto cleanup_workers;
        }

        static api_state_t api_state;
        api_state.config = &cfg;
        strncpy(api_state.current_mode,
                mode == MODE_JIGGLE ? "jiggle" : mode == MODE_TYPE ? "type" : "both",
                sizeof(api_state.current_mode) - 1);
        api_state.start_time = time(NULL);
        api_state.last_typing_session = 0;
        api_state.transport = g_transport;
        api_state.jiggler = g_jiggler;
        api_state.typer = g_typer;
        strncpy(api_state.api_key, resolved_key, sizeof(api_state.api_key) - 1);
        api_state.rate_limit = cfg.api.rate_limit;

        g_api = api_start(&api_state, cfg.api.host, cfg.api.port);
        if (!g_api) {
            fprintf(stderr, "[pikey] Warning: API server failed to start\n");
        }
    }

    fprintf(stderr, "[pikey] Running — Ctrl+C to stop\n");

    /* Main loop: just sleep */
    while (g_running) {
        sleep(1);
    }

    /* Cleanup */
cleanup_workers:
    /* Stop API server */
    if (g_api) {
        api_stop(g_api);
        g_api = NULL;
    }

    if (g_jiggler) {
        jiggler_stop(g_jiggler);
        jiggler_destroy(g_jiggler);
    }
    if (g_typer) {
        typer_stop(g_typer);
        typer_destroy(g_typer);
    }

    g_transport->disconnect(g_transport);
    if (transport_type == TRANSPORT_BT) {
        bt_transport_destroy(g_transport);
    } else {
        usb_transport_destroy(g_transport);
    }

    llm_client_cleanup();

    fprintf(stderr, "[pikey] Goodbye\n");
    return 0;
}
