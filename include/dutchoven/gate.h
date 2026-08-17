#ifndef DUTCHOVEN_GATE_H
#define DUTCHOVEN_GATE_H

#include <stddef.h>

/* Fixed limits keep configuration ownership simple and make the runtime bounds auditable. */
#define DO_PATH_MAX 1024
#define DO_ERROR_MAX 512
#define DO_GATE_MAX_APPS 16

/* Profiles are operator-facing presets; explicit timing flags may override their values. */
typedef enum {
    DO_GATE_PROFILE_LIGHT,
    DO_GATE_PROFILE_BROWNOUT,
    DO_GATE_PROFILE_HEAVY,
    DO_GATE_PROFILE_BLACKOUT
} do_gate_profile;

/*
 * A validated gate configuration. After do_gate_parse succeeds:
 *   1 <= block_ms <= period_ms <= 60000
 *   period_ms <= duration_ms <= 900000
 *   app_count is in the range 1..DO_GATE_MAX_APPS
 */
typedef struct {
    char apps[DO_GATE_MAX_APPS][DO_PATH_MAX];
    size_t app_count;
    unsigned period_ms;
    unsigned block_ms;
    unsigned duration_ms;
    unsigned warmup_ms;
    do_gate_profile profile;
    int timing_overridden;
    int dry_run;
    int json_output;
} do_gate_config;

/* Populate the default brownout profile without performing validation or I/O. */
void do_gate_defaults(do_gate_config *config);

/* Parse flags into config. Returns 0 on success and writes a diagnostic on failure. */
int do_gate_parse(int argc, char **argv, do_gate_config *config,
                  char *error, size_t error_capacity);

/* Print the resolved configuration, including profile overrides and effective mode. */
void do_gate_print(const do_gate_config *config);

/* Apply the bounded schedule. On non-Windows hosts, only dry-run is supported. */
int do_gate_run(const do_gate_config *config, char *error, size_t error_capacity);

#endif
