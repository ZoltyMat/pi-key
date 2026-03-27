/*
 * typer.h — LLM-powered typing engine thread
 */
#ifndef PIKEY_TYPER_H
#define PIKEY_TYPER_H

#include "transport.h"
#include "config.h"

typedef struct typer typer_t;

/*
 * Create a typer instance. Returns NULL on failure.
 */
typer_t *typer_create(hid_transport_t *transport, const typer_config_t *tcfg, const llm_config_t *lcfg);

/*
 * Start the typer thread. Returns 0 on success.
 */
int typer_start(typer_t *t);

/*
 * Stop the typer thread and wait for it to exit.
 */
void typer_stop(typer_t *t);

/*
 * Free typer resources. Must be stopped first.
 */
void typer_destroy(typer_t *t);

#endif /* PIKEY_TYPER_H */
