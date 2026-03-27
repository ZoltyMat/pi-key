/*
 * jiggler.c — Mouse jiggler thread
 *
 * Sends small, randomized mouse movements on a configurable interval.
 * Movements are intentionally tiny and varied to appear human/natural.
 */
#include "jiggler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

struct jiggler {
    hid_transport_t *transport;
    jiggler_config_t cfg;
    pthread_t thread;
    volatile int running;
};

static double rand_uniform(double lo, double hi) {
    return lo + ((double)rand() / (double)RAND_MAX) * (hi - lo);
}

static int rand_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (rand() % (hi - lo + 1));
}

static void jiggle_once(jiggler_t *j) {
    int max_d = j->cfg.max_delta;
    int dx, dy;

    if (rand_uniform(0, 1) < j->cfg.big_move_chance) {
        /* Occasional bigger move (still subtle) */
        max_d = rand_range(10, 20);
    }

    dx = rand_range(-max_d, max_d);
    dy = rand_range(-max_d, max_d);

    /* Avoid zero movement */
    if (dx == 0 && dy == 0) dx = 1;

    /* Move */
    j->transport->send_mouse(j->transport, 0, (int8_t)dx, (int8_t)dy, 0);

    /* Small delay */
    usleep((useconds_t)(rand_uniform(80000, 200000)));

    /* Return (with slight offset) */
    int ret_x = -dx + rand_range(-1, 1);
    int ret_y = -dy + rand_range(-1, 1);
    j->transport->send_mouse(j->transport, 0, (int8_t)ret_x, (int8_t)ret_y, 0);
}

static void *jiggler_thread(void *arg) {
    jiggler_t *j = (jiggler_t *)arg;

    fprintf(stderr, "[jiggler] Started\n");

    while (j->running) {
        double interval = rand_uniform(j->cfg.interval_min, j->cfg.interval_max);

        /* Wait in 1-second chunks so we can respond to stop quickly */
        double deadline = (double)time(NULL) + interval;
        while ((double)time(NULL) < deadline) {
            if (!j->running) goto done;
            usleep(1000000);  /* 1 second */
        }

        if (!j->running) break;

        jiggle_once(j);
    }

done:
    fprintf(stderr, "[jiggler] Stopped\n");
    return NULL;
}

jiggler_t *jiggler_create(hid_transport_t *transport, const jiggler_config_t *cfg) {
    jiggler_t *j = calloc(1, sizeof(jiggler_t));
    if (!j) return NULL;

    j->transport = transport;
    memcpy(&j->cfg, cfg, sizeof(jiggler_config_t));
    j->running = 0;

    return j;
}

int jiggler_start(jiggler_t *j) {
    if (!j->cfg.enabled) {
        fprintf(stderr, "[jiggler] Disabled in config\n");
        return 0;
    }

    j->running = 1;
    if (pthread_create(&j->thread, NULL, jiggler_thread, j) != 0) {
        fprintf(stderr, "[jiggler] Failed to create thread\n");
        j->running = 0;
        return -1;
    }

    return 0;
}

void jiggler_stop(jiggler_t *j) {
    if (!j->running) return;
    j->running = 0;
    pthread_join(j->thread, NULL);
}

void jiggler_destroy(jiggler_t *j) {
    if (!j) return;
    free(j);
}
