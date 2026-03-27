/*
 * jiggler.h — Mouse jiggler thread
 */
#ifndef PIKEY_JIGGLER_H
#define PIKEY_JIGGLER_H

#include "transport.h"
#include "config.h"

typedef struct jiggler jiggler_t;

/*
 * Create a jiggler instance. Returns NULL on failure.
 */
jiggler_t *jiggler_create(hid_transport_t *transport, const jiggler_config_t *cfg);

/*
 * Start the jiggler thread. Returns 0 on success.
 */
int jiggler_start(jiggler_t *j);

/*
 * Stop the jiggler thread and wait for it to exit.
 */
void jiggler_stop(jiggler_t *j);

/*
 * Free jiggler resources. Must be stopped first.
 */
void jiggler_destroy(jiggler_t *j);

#endif /* PIKEY_JIGGLER_H */
