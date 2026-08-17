#include "dutchoven/gate.h"

#include <stdio.h>
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
    CHECK(do_gate_parse(2, missing_target, &config, error, sizeof(error)) != 0,
          "missing application target is rejected");
    CHECK(do_gate_parse(6, bad_timing, &config, error, sizeof(error)) != 0,
          "block interval longer than period is rejected");
    CHECK(do_gate_parse(4, negative_timing, &config, error, sizeof(error)) != 0,
          "negative timing is rejected");
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

int main(void) {
    test_valid_gate();
    test_rejections();
    test_profiles_and_overrides();
    if (failures != 0) {
        (void)fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All DutchOven tests passed.\n");
    return 0;
}
