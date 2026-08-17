#include "dutchoven/gate.h"

#include <stdio.h>
#include <string.h>

#define DUTCHOVEN_VERSION "0.4.0"

/* Keep command rendering in one place so parse failures and --help never drift apart. */
static void usage(FILE *stream) {
    (void)fprintf(stream,
                  "DutchOven %s - application-scoped Windows network brownouts\n\n"
                  "Usage:\n"
                  "  dutchoven --app <absolute.exe> [--app <absolute.exe> ...] [options]\n"
                  "  dutchoven version\n\n"
                  "Options:\n"
                  "  --profile <name>     light | brownout (default) | heavy | blackout\n"
                  "  --period-ms <ms>     Override the profile period\n"
                  "  --block-ms <ms>      Override blocked time per period\n"
                  "  --duration-ms <ms>   Override total runtime\n"
                  "  --warmup-ms <ms>     Pass-through time before the first block\n"
                  "  --dry-run            Validate and print without changing WFP\n\n"
                  "Quick start:\n"
                  "  dutchoven --app C:\\Program Files\\Contoso\\TelemetryAgent.exe\n\n"
                  "Filters live only in a dynamic WFP session and are removed when the process exits.\n",
                  DUTCHOVEN_VERSION);
}

/* Exit 2 denotes invalid operator input; exit 1 denotes a runtime/WFP failure. */
static int run_gate(int argc, char **argv) {
    char error[DO_ERROR_MAX];
    do_gate_config config;

    if (do_gate_parse(argc, argv, &config, error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "error: %s\n\n", error);
        usage(stderr);
        return 2;
    }
    do_gate_print(&config);
    if (do_gate_run(&config, error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "version") == 0) {
        printf("DutchOven %s\n", DUTCHOVEN_VERSION);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }

    /* The tool has one operation, so flags begin the gate command without a subcommand. */
    if (argc >= 2 && strncmp(argv[1], "--", 2U) == 0) {
        return run_gate(argc - 1, argv + 1);
    }
    usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? 2 : 0;
}
