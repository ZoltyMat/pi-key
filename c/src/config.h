/*
 * config.h — YAML configuration parser for PiKey
 */
#ifndef PIKEY_CONFIG_H
#define PIKEY_CONFIG_H

#include <stdbool.h>

#define MAX_PROMPTS 16
#define MAX_STR_LEN 256

typedef struct {
    char name[MAX_STR_LEN];
    char cod[32];
    char target_mac[18];
} device_config_t;

typedef struct {
    bool enabled;
    double interval_min;
    double interval_max;
    int max_delta;
    double big_move_chance;
} jiggler_config_t;

typedef struct {
    bool enabled;
    double interval_min;
    double interval_max;
    int cpm_min;
    int cpm_max;
    double typo_rate;
    double think_pause_chance;
    double think_pause_min;
    double think_pause_max;
} typer_config_t;

typedef struct {
    char url[MAX_STR_LEN];
    char api_style[32];       /* "openai" or "ollama" */
    char model[MAX_STR_LEN];
    char api_key[MAX_STR_LEN];
    int max_tokens;
    char prompts[MAX_PROMPTS][MAX_STR_LEN];
    int num_prompts;
} llm_config_t;

typedef struct {
    bool enabled;
    char host[64];
    int port;
    char api_key[MAX_STR_LEN];
    int rate_limit;
    bool tls_enabled;
    char tls_cert_path[MAX_STR_LEN];
    char tls_key_path[MAX_STR_LEN];
} api_config_t;

typedef struct {
    device_config_t device;
    jiggler_config_t jiggler;
    typer_config_t typer;
    llm_config_t llm;
    api_config_t api;
} pikey_config_t;

/*
 * Parse a YAML config file into a pikey_config_t.
 * Returns 0 on success, -1 on error.
 * Struct is initialized with defaults before parsing.
 */
int parse_config(const char *path, pikey_config_t *cfg);

/*
 * Initialize config with default values.
 */
void config_defaults(pikey_config_t *cfg);

#endif /* PIKEY_CONFIG_H */
