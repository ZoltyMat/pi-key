/*
 * config.c — YAML config parser using libyaml
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

void config_defaults(pikey_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    /* device */
    strncpy(cfg->device.name, "Logitech K380 Multi-Device Keyboard", MAX_STR_LEN - 1);
    strncpy(cfg->device.cod, "0x002540", sizeof(cfg->device.cod) - 1);

    /* jiggler */
    cfg->jiggler.enabled = true;
    cfg->jiggler.interval_min = 45.0;
    cfg->jiggler.interval_max = 90.0;
    cfg->jiggler.max_delta = 3;
    cfg->jiggler.big_move_chance = 0.1;

    /* typer */
    cfg->typer.enabled = true;
    cfg->typer.interval_min = 180.0;
    cfg->typer.interval_max = 600.0;
    cfg->typer.cpm_min = 220;
    cfg->typer.cpm_max = 360;
    cfg->typer.typo_rate = 0.02;
    cfg->typer.think_pause_chance = 0.05;
    cfg->typer.think_pause_min = 1.5;
    cfg->typer.think_pause_max = 4.0;

    /* llm */
    strncpy(cfg->llm.api_style, "openai", sizeof(cfg->llm.api_style) - 1);
    cfg->llm.max_tokens = 200;
    strncpy(cfg->llm.prompts[0],
            "Write a realistic Python function with a docstring and comments.",
            MAX_STR_LEN - 1);
    cfg->llm.num_prompts = 1;
}

/* Parser state machine for navigating YAML structure */
typedef enum {
    STATE_ROOT,
    STATE_DEVICE,
    STATE_JIGGLER,
    STATE_TYPER,
    STATE_LLM,
    STATE_LLM_PROMPTS,
} parse_state_t;

int parse_config(const char *path, pikey_config_t *cfg) {
    FILE *fp = NULL;
    yaml_parser_t parser;
    yaml_event_t event;
    parse_state_t state = STATE_ROOT;
    char current_key[MAX_STR_LEN] = {0};
    int done = 0;
    int ret = -1;

    config_defaults(cfg);

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Config not found: %s\n  Run: cp config.example.yaml config.yaml\n", path);
        return -1;
    }

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Failed to initialize YAML parser\n");
        fclose(fp);
        return -1;
    }

    yaml_parser_set_input_file(&parser, fp);

    /* When we enter LLM_PROMPTS, reset the prompt count */
    int prompts_reset = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "YAML parse error: %s\n", parser.problem);
            goto cleanup;
        }

        switch (event.type) {
        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;

            if (state == STATE_ROOT) {
                /* Top-level key */
                if (strcmp(val, "device") == 0) state = STATE_DEVICE;
                else if (strcmp(val, "jiggler") == 0) state = STATE_JIGGLER;
                else if (strcmp(val, "typer") == 0) state = STATE_TYPER;
                else if (strcmp(val, "llm") == 0) state = STATE_LLM;
                else strncpy(current_key, val, MAX_STR_LEN - 1);
            }
            else if (state == STATE_LLM_PROMPTS) {
                /* Each scalar in prompts array */
                if (cfg->llm.num_prompts < MAX_PROMPTS) {
                    strncpy(cfg->llm.prompts[cfg->llm.num_prompts], val, MAX_STR_LEN - 1);
                    cfg->llm.num_prompts++;
                }
            }
            else if (current_key[0] != '\0') {
                /* We have a key, this is the value */
                switch (state) {
                case STATE_DEVICE:
                    if (strcmp(current_key, "name") == 0)
                        strncpy(cfg->device.name, val, MAX_STR_LEN - 1);
                    else if (strcmp(current_key, "cod") == 0)
                        strncpy(cfg->device.cod, val, sizeof(cfg->device.cod) - 1);
                    else if (strcmp(current_key, "target_mac") == 0)
                        strncpy(cfg->device.target_mac, val, sizeof(cfg->device.target_mac) - 1);
                    break;

                case STATE_JIGGLER:
                    if (strcmp(current_key, "enabled") == 0)
                        cfg->jiggler.enabled = (strcmp(val, "true") == 0 || strcmp(val, "True") == 0);
                    else if (strcmp(current_key, "interval_min") == 0)
                        cfg->jiggler.interval_min = atof(val);
                    else if (strcmp(current_key, "interval_max") == 0)
                        cfg->jiggler.interval_max = atof(val);
                    else if (strcmp(current_key, "max_delta") == 0)
                        cfg->jiggler.max_delta = atoi(val);
                    else if (strcmp(current_key, "big_move_chance") == 0)
                        cfg->jiggler.big_move_chance = atof(val);
                    break;

                case STATE_TYPER:
                    if (strcmp(current_key, "enabled") == 0)
                        cfg->typer.enabled = (strcmp(val, "true") == 0 || strcmp(val, "True") == 0);
                    else if (strcmp(current_key, "interval_min") == 0)
                        cfg->typer.interval_min = atof(val);
                    else if (strcmp(current_key, "interval_max") == 0)
                        cfg->typer.interval_max = atof(val);
                    else if (strcmp(current_key, "cpm_min") == 0)
                        cfg->typer.cpm_min = atoi(val);
                    else if (strcmp(current_key, "cpm_max") == 0)
                        cfg->typer.cpm_max = atoi(val);
                    else if (strcmp(current_key, "typo_rate") == 0)
                        cfg->typer.typo_rate = atof(val);
                    else if (strcmp(current_key, "think_pause_chance") == 0)
                        cfg->typer.think_pause_chance = atof(val);
                    break;

                case STATE_LLM:
                    if (strcmp(current_key, "url") == 0)
                        strncpy(cfg->llm.url, val, MAX_STR_LEN - 1);
                    else if (strcmp(current_key, "api_style") == 0)
                        strncpy(cfg->llm.api_style, val, sizeof(cfg->llm.api_style) - 1);
                    else if (strcmp(current_key, "model") == 0)
                        strncpy(cfg->llm.model, val, MAX_STR_LEN - 1);
                    else if (strcmp(current_key, "api_key") == 0)
                        strncpy(cfg->llm.api_key, val, MAX_STR_LEN - 1);
                    else if (strcmp(current_key, "max_tokens") == 0)
                        cfg->llm.max_tokens = atoi(val);
                    else if (strcmp(current_key, "prompts") == 0) {
                        /* prompts is a sequence, handled by SEQUENCE_START */
                    }
                    break;

                default:
                    break;
                }
                current_key[0] = '\0';
            }
            else {
                /* This is a key name within a mapping */
                strncpy(current_key, val, MAX_STR_LEN - 1);

                /* Special: "prompts" in LLM triggers sequence state transition */
                if (state == STATE_LLM && strcmp(val, "prompts") == 0) {
                    /* Will transition on SEQUENCE_START */
                }

                /* think_pause_secs is a sequence in the typer section */
                if (state == STATE_TYPER && strcmp(val, "think_pause_secs") == 0) {
                    /* Handled inline below */
                }
            }
            break;
        }

        case YAML_SEQUENCE_START_EVENT:
            if (state == STATE_LLM && strcmp(current_key, "prompts") == 0) {
                state = STATE_LLM_PROMPTS;
                if (!prompts_reset) {
                    cfg->llm.num_prompts = 0;
                    prompts_reset = 1;
                }
                current_key[0] = '\0';
            }
            else if (state == STATE_TYPER && strcmp(current_key, "think_pause_secs") == 0) {
                /* Read next two scalars as min/max */
                current_key[0] = '\0';
                /* Parse inline: read events until SEQUENCE_END */
                yaml_event_delete(&event);
                int idx = 0;
                while (1) {
                    if (!yaml_parser_parse(&parser, &event)) goto cleanup;
                    if (event.type == YAML_SEQUENCE_END_EVENT) break;
                    if (event.type == YAML_SCALAR_EVENT) {
                        double v = atof((const char *)event.data.scalar.value);
                        if (idx == 0) cfg->typer.think_pause_min = v;
                        else if (idx == 1) cfg->typer.think_pause_max = v;
                        idx++;
                    }
                    yaml_event_delete(&event);
                }
            }
            break;

        case YAML_SEQUENCE_END_EVENT:
            if (state == STATE_LLM_PROMPTS) {
                state = STATE_LLM;
            }
            break;

        case YAML_MAPPING_START_EVENT:
            /* Already handled by state transitions in SCALAR */
            break;

        case YAML_MAPPING_END_EVENT:
            if (state != STATE_ROOT && state != STATE_LLM_PROMPTS) {
                state = STATE_ROOT;
                current_key[0] = '\0';
            }
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    ret = 0;

cleanup:
    yaml_parser_delete(&parser);
    fclose(fp);
    return ret;
}
