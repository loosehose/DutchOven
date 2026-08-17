#include "dutchoven/gate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message)                                                          \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            (void)fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__);    \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

static void test_valid_gate(void) {
    char *arguments[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe",
        "--app", "C:\\Program Files\\Contoso\\TelemetryHelper.exe",
        "--period-ms", "4000",
        "--block-ms", "1250",
        "--duration-ms", "12000",
        "--warmup-ms", "500",
        "--dry-run"
    };
    do_gate_config config;
    char error[DO_ERROR_MAX];
    int count = (int)(sizeof(arguments) / sizeof(arguments[0]));
    CHECK(do_gate_parse(count, arguments, &config, error, sizeof(error)) == 0,
          "valid gate command parses");
    CHECK(config.app_count == 2U, "all explicit applications are retained");
    CHECK(config.period_ms == 4000U && config.block_ms == 1250U,
          "gate duty cycle is retained");
    CHECK(config.duration_ms == 12000U && config.warmup_ms == 500U,
          "gate runtime bounds are retained");
    CHECK(config.dry_run != 0, "dry-run flag is retained");
}

static void test_rejections(void) {
    do_gate_config config;
    char error[DO_ERROR_MAX];
    char *missing_target[] = {"--profile", "brownout"};
    char *bad_timing[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe",
        "--period-ms", "1000", "--block-ms", "1001"
    };
    char *negative_timing[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe",
        "--period-ms", "-1"
    };
    char *unknown_option[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe", "--unknown", "value"
    };
    char *relative_path[] = {"--app", ".\\TelemetryAgent.exe"};
    CHECK(do_gate_parse(2, missing_target, &config, error, sizeof(error)) != 0,
          "missing application target is rejected");
    CHECK(do_gate_parse(6, bad_timing, &config, error, sizeof(error)) != 0,
          "block interval longer than period is rejected");
    CHECK(do_gate_parse(4, negative_timing, &config, error, sizeof(error)) != 0,
          "negative timing is rejected");
    CHECK(do_gate_parse(4, unknown_option, &config, error, sizeof(error)) != 0,
          "unknown option is rejected");
    CHECK(do_gate_parse(2, relative_path, &config, error, sizeof(error)) != 0,
          "relative target path is rejected");
}

static void test_profiles_and_overrides(void) {
    char *heavy[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe", "--profile", "heavy"
    };
    /* Explicit timing must win even when it appears before the profile on the command line. */
    char *order_independent[] = {
        "--block-ms", "2000", "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe",
        "--profile", "light"
    };
    char *invalid[] = {
        "--app", "C:\\Program Files\\Contoso\\TelemetryAgent.exe", "--profile", "unknown"
    };
    do_gate_config config;
    char error[DO_ERROR_MAX];
    CHECK(do_gate_parse(4, heavy, &config, error, sizeof(error)) == 0,
          "named heavy profile parses");
    CHECK(config.profile == DO_GATE_PROFILE_HEAVY && config.period_ms == 5000U &&
              config.block_ms == 3500U && config.duration_ms == 60000U,
          "heavy profile applies baked timing");
    CHECK(do_gate_parse(6, order_independent, &config, error, sizeof(error)) == 0,
          "timing override and profile parse in either order");
    CHECK(config.profile == DO_GATE_PROFILE_LIGHT && config.block_ms == 2000U &&
              config.timing_overridden != 0,
          "explicit timing wins over the selected profile");
    CHECK(do_gate_parse(4, invalid, &config, error, sizeof(error)) != 0,
          "unknown profile is rejected");
}

static void test_timing_boundaries(void) {
    char *minimum[] = {
        "--app", "C:\\Canary.exe", "--period-ms", "100", "--block-ms", "1",
        "--duration-ms", "100", "--warmup-ms", "60000"
    };
    char *maximum[] = {
        "--app", "C:\\Canary.exe", "--period-ms", "60000", "--block-ms", "60000",
        "--duration-ms", "900000", "--warmup-ms", "60000", "--json"
    };
    char *period_low[] = {"--app", "C:\\Canary.exe", "--period-ms", "99"};
    char *period_high[] = {"--app", "C:\\Canary.exe", "--period-ms", "60001"};
    char *block_zero[] = {"--app", "C:\\Canary.exe", "--block-ms", "0"};
    char *duration_high[] = {"--app", "C:\\Canary.exe", "--duration-ms", "900001"};
    char *warmup_high[] = {"--app", "C:\\Canary.exe", "--warmup-ms", "60001"};
    do_gate_config config;
    char error[DO_ERROR_MAX];

    CHECK(do_gate_parse(10, minimum, &config, error, sizeof(error)) == 0,
          "minimum timing boundary parses");
    CHECK(do_gate_parse(11, maximum, &config, error, sizeof(error)) == 0,
          "maximum timing boundary parses");
    CHECK(config.json_output != 0, "JSON output flag is retained");
    CHECK(do_gate_parse(4, period_low, &config, error, sizeof(error)) != 0,
          "period below minimum is rejected");
    CHECK(do_gate_parse(4, period_high, &config, error, sizeof(error)) != 0,
          "period above maximum is rejected");
    CHECK(do_gate_parse(4, block_zero, &config, error, sizeof(error)) != 0,
          "zero block interval is rejected");
    CHECK(do_gate_parse(4, duration_high, &config, error, sizeof(error)) != 0,
          "duration above maximum is rejected");
    CHECK(do_gate_parse(4, warmup_high, &config, error, sizeof(error)) != 0,
          "warmup above maximum is rejected");
}

static void test_target_boundaries(void) {
    char paths[DO_GATE_MAX_APPS + 1U][64];
    char *arguments[(DO_GATE_MAX_APPS + 1U) * 2U];
    char *duplicate[] = {"--app", "C:\\Canary.exe", "--app", "c:/canary.exe"};
    char long_path[DO_PATH_MAX + 1U];
    char *too_long[] = {"--app", long_path};
    do_gate_config config;
    char error[DO_ERROR_MAX];

    for (size_t index = 0; index < DO_GATE_MAX_APPS + 1U; index++) {
        (void)snprintf(paths[index], sizeof(paths[index]), "C:\\Canary-%u.exe",
                       (unsigned)index);
        arguments[index * 2U] = "--app";
        arguments[index * 2U + 1U] = paths[index];
    }
    CHECK(do_gate_parse((int)(DO_GATE_MAX_APPS * 2U), arguments, &config,
                        error, sizeof(error)) == 0,
          "maximum target count parses");
    CHECK(config.app_count == DO_GATE_MAX_APPS, "maximum target count is retained");
    CHECK(do_gate_parse((int)((DO_GATE_MAX_APPS + 1U) * 2U), arguments, &config,
                        error, sizeof(error)) != 0,
          "target count above maximum is rejected");
    CHECK(do_gate_parse(4, duplicate, &config, error, sizeof(error)) != 0,
          "case-insensitive duplicate target is rejected");

    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = 'C';
    long_path[1] = ':';
    long_path[2] = '\\';
    long_path[sizeof(long_path) - 1U] = '\0';
    CHECK(do_gate_parse(2, too_long, &config, error, sizeof(error)) != 0,
          "target path at storage capacity is rejected");
}

int main(void) {
    test_valid_gate();
    test_rejections();
    test_profiles_and_overrides();
    test_timing_boundaries();
    test_target_boundaries();
    if (failures != 0) {
        (void)fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All DutchOven tests passed.\n");
    return 0;
}
