/*
 * llm_client.h — HTTP client for LLM APIs (OpenAI / Ollama)
 */
#ifndef PIKEY_LLM_CLIENT_H
#define PIKEY_LLM_CLIENT_H

#include "config.h"

/*
 * Fetch generated text from the configured LLM endpoint.
 * prompt: the user prompt to send.
 * cfg: LLM configuration (url, api_style, model, api_key, max_tokens).
 *
 * Returns a malloc'd string with the response text, or NULL on error.
 * Caller must free() the returned string.
 */
char *llm_fetch_text(const char *prompt, const llm_config_t *cfg);

/*
 * Initialize the LLM client (call once at startup, calls curl_global_init).
 */
void llm_client_init(void);

/*
 * Cleanup the LLM client (call once at shutdown).
 */
void llm_client_cleanup(void);

#endif /* PIKEY_LLM_CLIENT_H */
