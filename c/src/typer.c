/*
 * typer.c — LLM-powered typing engine thread
 *
 * Fetches generated text from an LLM endpoint and types it character
 * by character with human-like timing, typo simulation, and thinking pauses.
 */
#include "typer.h"
#include "keymap.h"
#include "llm_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

struct typer {
    hid_transport_t *transport;
    typer_config_t cfg;
    llm_config_t llm_cfg;
    pthread_t thread;
    volatile int running;
};

static double rand_uniform(double lo, double hi) {
    return lo + ((double)rand() / (double)RAND_MAX) * (hi - lo);
}

/* Simple Box-Muller for Gaussian jitter */
static double rand_gauss(double mean, double stddev) {
    double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 1.0);
    double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 1.0);
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return mean + z * stddev;
}

static double char_delay(const typer_config_t *cfg) {
    double cpm = rand_uniform(cfg->cpm_min, cfg->cpm_max);
    double base = 60.0 / cpm;
    double jitter = rand_gauss(0, base * 0.2);
    double delay = base + jitter;
    return (delay < 0.02) ? 0.02 : delay;
}

static int should_typo(const typer_config_t *cfg, char c) {
    char lower = (char)tolower((unsigned char)c);
    /* Only typo on letters that have neighbors */
    if (lower < 'a' || lower > 'z') return 0;
    return (rand_uniform(0, 1) < cfg->typo_rate);
}

static void send_char(hid_transport_t *transport, char c) {
    uint8_t report[8];
    uint8_t release[8];

    if (char_to_hid_report(c, report) == 0) {
        transport->send_keyboard(transport, report);
        usleep(8000);  /* 8ms between press and release */
        release_report(release);
        transport->send_keyboard(transport, release);
    }
}

static void send_backspace(hid_transport_t *transport) {
    uint8_t report[8] = { 0x01, 0x00, 0x00, HID_KEY_BACKSPACE, 0, 0, 0, 0 };
    uint8_t release[8];

    transport->send_keyboard(transport, report);
    usleep(8000);
    release_report(release);
    transport->send_keyboard(transport, release);
}

static void type_text(typer_t *t, const char *text) {
    for (size_t i = 0; text[i] && t->running; i++) {
        char c = text[i];

        /* Random thinking pause */
        if (rand_uniform(0, 1) < t->cfg.think_pause_chance) {
            double pause = rand_uniform(t->cfg.think_pause_min, t->cfg.think_pause_max);
            usleep((useconds_t)(pause * 1000000.0));
        }

        /* Typo simulation */
        if (should_typo(&t->cfg, c)) {
            char wrong = get_neighbor_key(c);
            send_char(t->transport, wrong);
            usleep((useconds_t)(char_delay(&t->cfg) * 1.5 * 1000000.0));
            send_backspace(t->transport);
            usleep((useconds_t)(rand_uniform(0.1, 0.3) * 1000000.0));
        }

        send_char(t->transport, c);
        usleep((useconds_t)(char_delay(&t->cfg) * 1000000.0));
    }
}

static const char *pick_prompt(const llm_config_t *cfg) {
    if (cfg->num_prompts <= 0) {
        return "Write a realistic Python function with a docstring and comments.";
    }
    return cfg->prompts[rand() % cfg->num_prompts];
}

static void *typer_thread(void *arg) {
    typer_t *t = (typer_t *)arg;

    fprintf(stderr, "[typer] Started\n");

    while (t->running) {
        double interval = rand_uniform(t->cfg.interval_min, t->cfg.interval_max);

        /* Wait in 1-second chunks */
        double deadline = (double)time(NULL) + interval;
        while ((double)time(NULL) < deadline) {
            if (!t->running) goto done;
            usleep(1000000);
        }

        if (!t->running) break;

        /* Fetch text from LLM */
        const char *prompt = pick_prompt(&t->llm_cfg);
        fprintf(stderr, "[typer] Fetching LLM text...\n");

        char *text = llm_fetch_text(prompt, &t->llm_cfg);
        if (text && text[0]) {
            fprintf(stderr, "[typer] Typing %zu chars...\n", strlen(text));
            type_text(t, text);
            fprintf(stderr, "[typer] Typing session complete\n");
            free(text);
        } else {
            fprintf(stderr, "[typer] LLM returned empty text, skipping\n");
            if (text) free(text);
            /* Back off on error */
            usleep(60000000);  /* 60 seconds */
        }
    }

done:
    fprintf(stderr, "[typer] Stopped\n");
    return NULL;
}

typer_t *typer_create(hid_transport_t *transport, const typer_config_t *tcfg, const llm_config_t *lcfg) {
    typer_t *t = calloc(1, sizeof(typer_t));
    if (!t) return NULL;

    t->transport = transport;
    memcpy(&t->cfg, tcfg, sizeof(typer_config_t));
    memcpy(&t->llm_cfg, lcfg, sizeof(llm_config_t));
    t->running = 0;

    return t;
}

int typer_start(typer_t *t) {
    if (!t->cfg.enabled) {
        fprintf(stderr, "[typer] Disabled in config\n");
        return 0;
    }

    if (!t->llm_cfg.url[0]) {
        fprintf(stderr, "[typer] Error: llm.url not set in config\n");
        return -1;
    }

    t->running = 1;
    if (pthread_create(&t->thread, NULL, typer_thread, t) != 0) {
        fprintf(stderr, "[typer] Failed to create thread\n");
        t->running = 0;
        return -1;
    }

    return 0;
}

void typer_stop(typer_t *t) {
    if (!t->running) return;
    t->running = 0;
    pthread_join(t->thread, NULL);
}

void typer_destroy(typer_t *t) {
    if (!t) return;
    free(t);
}
