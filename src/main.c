#include "dutchoven/gate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define DUTCHOVEN_VERSION "0.4.1"

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
                  "  --dry-run            Validate and print without changing WFP\n"
                  "  --json               Emit machine-readable JSON Lines\n\n"
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

static int dutchoven_main(int argc, char **argv) {
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

#ifdef _WIN32

/*
 * Windows' narrow argv follows the active ANSI code page and cannot represent every valid path.
 * Enter through wmain, normalize arguments to UTF-8, and keep the portable parser unchanged.
 */
int wmain(int argc, wchar_t **wide_argv) {
    char **utf8_argv = NULL;
    int result = 1;

    (void)SetConsoleOutputCP(CP_UTF8);
    utf8_argv = (char **)calloc((size_t)argc + 1U, sizeof(*utf8_argv));
    if (utf8_argv == NULL) {
        (void)fprintf(stderr, "error: cannot allocate argument table\n");
        return 1;
    }
    for (int index = 0; index < argc; index++) {
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[index], -1,
                                         NULL, 0, NULL, NULL);
        if (length <= 0) {
            (void)fprintf(stderr, "error: cannot encode command-line argument %d as UTF-8\n",
                          index);
            goto cleanup;
        }
        utf8_argv[index] = (char *)malloc((size_t)length);
        if (utf8_argv[index] == NULL ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[index], -1,
                                utf8_argv[index], length, NULL, NULL) <= 0) {
            (void)fprintf(stderr, "error: cannot allocate command-line argument %d\n", index);
            goto cleanup;
        }
    }
    result = dutchoven_main(argc, utf8_argv);
cleanup:
    for (int index = 0; index < argc; index++) {
        free(utf8_argv[index]);
    }
    free(utf8_argv);
    return result;
}

#else

int main(int argc, char **argv) {
    return dutchoven_main(argc, argv);
}

#endif
