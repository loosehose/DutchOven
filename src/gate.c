#include "dutchoven/gate.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hard limits bound both operational impact and arithmetic used by the scheduler. */
#define DO_GATE_MIN_PERIOD_MS 100U
#define DO_GATE_MAX_PERIOD_MS 60000U
#define DO_GATE_MAX_DURATION_MS 900000U
#define DO_GATE_MAX_WARMUP_MS 60000U

void do_gate_defaults(do_gate_config *config) {
    memset(config, 0, sizeof(*config));
    config->profile = DO_GATE_PROFILE_BROWNOUT;
    config->period_ms = 5000U;
    config->block_ms = 1500U;
    config->duration_ms = 60000U;
}

static const char *gate_profile_name(do_gate_profile profile) {
    switch (profile) {
        case DO_GATE_PROFILE_LIGHT:
            return "light";
        case DO_GATE_PROFILE_BROWNOUT:
            return "brownout";
        case DO_GATE_PROFILE_HEAVY:
            return "heavy";
        case DO_GATE_PROFILE_BLACKOUT:
            return "blackout";
    }
    return "unknown";
}

static int parse_unsigned(const char *text, unsigned *value) {
    char *end = NULL;
    unsigned long parsed;

    /* strtoul accepts a leading minus sign, so reject it before conversion. */
    if (*text == '\0' || *text == '-') {
        return -1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT_MAX) {
        return -1;
    }
    *value = (unsigned)parsed;
    return 0;
}

static int copy_string(char *destination, size_t capacity, const char *source) {
    size_t length = strlen(source);

    if (capacity == 0U || length >= capacity) {
        return -1;
    }
    memcpy(destination, source, length + 1U);
    return 0;
}

static int gate_apply_profile(do_gate_config *config, const char *name) {
    /* Selecting a profile replaces every preset; explicit flags are applied afterward. */
    config->warmup_ms = 0U;
    if (strcmp(name, "light") == 0) {
        config->profile = DO_GATE_PROFILE_LIGHT;
        config->period_ms = 5000U;
        config->block_ms = 500U;
        config->duration_ms = 60000U;
        return 0;
    }
    if (strcmp(name, "brownout") == 0) {
        config->profile = DO_GATE_PROFILE_BROWNOUT;
        config->period_ms = 5000U;
        config->block_ms = 1500U;
        config->duration_ms = 60000U;
        return 0;
    }
    if (strcmp(name, "heavy") == 0) {
        config->profile = DO_GATE_PROFILE_HEAVY;
        config->period_ms = 5000U;
        config->block_ms = 3500U;
        config->duration_ms = 60000U;
        return 0;
    }
    if (strcmp(name, "blackout") == 0) {
        config->profile = DO_GATE_PROFILE_BLACKOUT;
        config->period_ms = 5000U;
        config->block_ms = 5000U;
        config->duration_ms = 30000U;
        return 0;
    }
    return -1;
}

static int parse_option_unsigned(const char *option, const char *text, unsigned *value,
                                 char *error, size_t error_capacity) {
    if (parse_unsigned(text, value) != 0) {
        (void)snprintf(error, error_capacity, "invalid value for %s: %s", option, text);
        return -1;
    }
    return 0;
}

static int local_absolute_windows_path(const char *path) {
    /* This is a lexical preflight. Windows resolves and verifies the file before WFP is opened. */
    return isalpha((unsigned char)path[0]) != 0 && path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/') && path[3] != '\0';
}

static int same_path_ascii(const char *left, const char *right) {
    /* Drive paths are case-insensitive; treating both separators alike catches duplicates early. */
    while (*left != '\0' && *right != '\0') {
        unsigned char first = (unsigned char)*left++;
        unsigned char second = (unsigned char)*right++;
        if (first == '/') {
            first = '\\';
        }
        if (second == '/') {
            second = '\\';
        }
        if (tolower(first) != tolower(second)) {
            return 0;
        }
    }
    return *left == '\0' && *right == '\0';
}

int do_gate_parse(int argc, char **argv, do_gate_config *config,
                  char *error, size_t error_capacity) {
    int profile_seen = 0;

    do_gate_defaults(config);

    /*
     * Resolve the profile first so timing overrides are independent of argument order.
     * The second pass owns all other validation and always wins for explicit timing.
     */
    for (int index = 0; index < argc; index++) {
        if (strcmp(argv[index], "--profile") == 0) {
            if (profile_seen != 0 || index + 1 >= argc ||
                gate_apply_profile(config, argv[index + 1]) != 0) {
                (void)snprintf(error, error_capacity,
                               "--profile must be one of: light, brownout, heavy, blackout");
                return -1;
            }
            profile_seen = 1;
            index++;
        }
    }
    for (int index = 0; index < argc; index++) {
        const char *option = argv[index];
        if (strcmp(option, "--dry-run") == 0) {
            config->dry_run = 1;
            continue;
        }
        if (strcmp(option, "--profile") == 0) {
            index++;
            continue;
        }
        if (index + 1 >= argc) {
            (void)snprintf(error, error_capacity, "missing value for %s", option);
            return -1;
        }
        const char *value = argv[++index];
        if (strcmp(option, "--app") == 0) {
            if (config->app_count >= DO_GATE_MAX_APPS || !local_absolute_windows_path(value) ||
                copy_string(config->apps[config->app_count], DO_PATH_MAX, value) != 0) {
                (void)snprintf(error, error_capacity,
                               "--app must be a local absolute Windows path (maximum %u)",
                               DO_GATE_MAX_APPS);
                return -1;
            }
            for (size_t prior = 0; prior < config->app_count; prior++) {
                if (same_path_ascii(config->apps[prior], value)) {
                    (void)snprintf(error, error_capacity, "duplicate --app target: %s", value);
                    return -1;
                }
            }
            config->app_count++;
        } else if (strcmp(option, "--period-ms") == 0) {
            if (parse_option_unsigned(option, value, &config->period_ms,
                                      error, error_capacity) != 0) {
                return -1;
            }
            config->timing_overridden = 1;
        } else if (strcmp(option, "--block-ms") == 0) {
            if (parse_option_unsigned(option, value, &config->block_ms,
                                      error, error_capacity) != 0) {
                return -1;
            }
            config->timing_overridden = 1;
        } else if (strcmp(option, "--duration-ms") == 0) {
            if (parse_option_unsigned(option, value, &config->duration_ms,
                                      error, error_capacity) != 0) {
                return -1;
            }
            config->timing_overridden = 1;
        } else if (strcmp(option, "--warmup-ms") == 0) {
            if (parse_option_unsigned(option, value, &config->warmup_ms,
                                      error, error_capacity) != 0) {
                return -1;
            }
            config->timing_overridden = 1;
        } else {
            (void)snprintf(error, error_capacity, "unknown gate option: %s", option);
            return -1;
        }
    }
    if (config->app_count == 0U) {
        (void)snprintf(error, error_capacity, "gate requires at least one --app");
        return -1;
    }
    if (config->period_ms < DO_GATE_MIN_PERIOD_MS ||
        config->period_ms > DO_GATE_MAX_PERIOD_MS || config->block_ms == 0U ||
        config->block_ms > config->period_ms || config->duration_ms < config->period_ms ||
        config->duration_ms > DO_GATE_MAX_DURATION_MS ||
        config->warmup_ms > DO_GATE_MAX_WARMUP_MS) {
        (void)snprintf(error, error_capacity,
                       "gate timing requires period=100..60000ms, block=1..period, "
                       "duration=period..900000ms, and warmup<=60000ms");
        return -1;
    }
    return 0;
}

void do_gate_print(const do_gate_config *config) {
    printf("Targets: %llu\n", (unsigned long long)config->app_count);
    for (size_t index = 0; index < config->app_count; index++) {
        printf("  %s\n", config->apps[index]);
    }
    printf("Schedule: block=%ums pass=%ums period=%ums duration=%ums warmup=%ums\n",
           config->block_ms, config->period_ms - config->block_ms,
           config->period_ms, config->duration_ms, config->warmup_ms);
    printf("Profile: %s%s\n", gate_profile_name(config->profile),
           config->timing_overridden != 0 ? " (with timing overrides)" : "");
    printf("Mode: %s%s\n", config->block_ms == config->period_ms ? "blackout" : "brownout",
           config->dry_run ? " (dry run)" : "");
}

#ifdef _WIN32

#include <fwpmu.h>
#include <rpc.h>
#include <windows.h>

#ifndef FWPM_SESSION_FLAG_DYNAMIC
#define FWPM_SESSION_FLAG_DYNAMIC UINT32_C(0x00000001)
#endif
#ifndef FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT
#define FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT UINT32_C(0x00000008)
#endif

/* Local GUID values avoid importing SDK GUID objects and keep the BOF and executable aligned. */
static const GUID do_layer_connect_v4 = {
    0xc38d57d1, 0x05a7, 0x4c33, {0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82}
};
static const GUID do_layer_connect_v6 = {
    0x4a72393b, 0x319f, 0x44bc, {0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4}
};
static const GUID do_condition_app_id = {
    0xd78e1e87, 0x8644, 0x4ea5, {0x94, 0x37, 0xd8, 0x09, 0xec, 0xef, 0xc9, 0x71}
};

typedef struct {
    /* This structure owns the engine, WFP-allocated AppIDs, and committed filter identifiers. */
    HANDLE engine;
    GUID sublayer_key;
    FWP_BYTE_BLOB *app_ids[DO_GATE_MAX_APPS];
    UINT64 filter_ids[DO_GATE_MAX_APPS * 2U];
    size_t filter_count;
} do_gate_engine;

static volatile LONG do_gate_stop_requested = 0L;

static unsigned long long gate_wall_ms(void) {
    /* Wall time is emitted for evidence correlation; scheduling uses monotonic GetTickCount64. */
    FILETIME value;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&value);
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return (unsigned long long)((ticks.QuadPart / 10000ULL) - 11644473600000ULL);
}

static void gate_flush_log(void) {
    (void)fflush(stdout);
}

static BOOL WINAPI gate_console_handler(DWORD signal) {
    /* Console handlers must stay minimal: request cancellation and let the main path clean up. */
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT ||
        signal == CTRL_LOGOFF_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        (void)InterlockedExchange(&do_gate_stop_requested, 1L);
        return TRUE;
    }
    return FALSE;
}

static int gate_stopping(void) {
    return InterlockedCompareExchange(&do_gate_stop_requested, 0L, 0L) != 0L;
}

static void gate_wait_until(ULONGLONG target) {
    /* Short waits keep Ctrl+C responsive without busy-spinning or accumulating relative drift. */
    while (!gate_stopping()) {
        ULONGLONG now = GetTickCount64();
        DWORD wait;
        if (now >= target) {
            break;
        }
        wait = (DWORD)(target - now > 50ULL ? 50ULL : target - now);
        Sleep(wait);
    }
}

static int gate_is_administrator(void) {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = NULL;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2U, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0U, 0U, 0U, 0U, 0U, 0U,
                                 &administrators) == 0) {
        return 0;
    }
    if (CheckTokenMembership(NULL, administrators, &member) == 0) {
        member = FALSE;
    }
    (void)FreeSid(administrators);
    return member != FALSE;
}

static int gate_prepare_app(const char *path, FWP_BYTE_BLOB **app_id,
                            char *error, size_t error_capacity) {
    char full_path[DO_PATH_MAX];
    wchar_t wide_path[DO_PATH_MAX];
    DWORD attributes;
    DWORD length = GetFullPathNameA(path, (DWORD)sizeof(full_path), full_path, NULL);
    int wide_length;
    DWORD result;
    if (length == 0U || length >= (DWORD)sizeof(full_path)) {
        (void)snprintf(error, error_capacity, "cannot resolve target path: %s", path);
        return -1;
    }
    attributes = GetFileAttributesA(full_path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        (void)snprintf(error, error_capacity, "target is not an executable file: %s", full_path);
        return -1;
    }
    wide_length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, full_path, -1,
                                      wide_path, (int)(sizeof(wide_path) / sizeof(wide_path[0])));
    if (wide_length <= 0) {
        (void)snprintf(error, error_capacity, "cannot encode target path: %s", full_path);
        return -1;
    }
    /* WFP matches its canonical AppID blob, not the operator's original path string. */
    result = FwpmGetAppIdFromFileName0(wide_path, app_id);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot derive WFP AppID for %s: %lu",
                       full_path, (unsigned long)result);
        return -1;
    }
    return 0;
}

static void gate_engine_close(do_gate_engine *gate) {
    /* Closing a dynamic session removes its sublayer and filters, even after explicit cleanup fails. */
    if (gate->engine != NULL) {
        (void)FwpmEngineClose0(gate->engine);
        gate->engine = NULL;
    }
    for (size_t index = 0; index < DO_GATE_MAX_APPS; index++) {
        if (gate->app_ids[index] != NULL) {
            FwpmFreeMemory0((void **)&gate->app_ids[index]);
        }
    }
    gate->filter_count = 0U;
}

static int gate_engine_open(do_gate_engine *gate, const do_gate_config *config,
                            char *error, size_t error_capacity) {
    FWPM_SESSION0 session;
    FWPM_SUBLAYER0 sublayer;
    RPC_STATUS uuid_result;
    DWORD result;
    memset(gate, 0, sizeof(*gate));
    memset(&session, 0, sizeof(session));
    session.displayData.name = L"DutchOven dynamic session";
    session.displayData.description = L"Non-persistent application-scoped brownout";
    /* Dynamic session ownership is the last-resort cleanup guarantee on process termination. */
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    session.txnWaitTimeoutInMSec = 3000U;
    result = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &gate->engine);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot open WFP engine: %lu",
                       (unsigned long)result);
        return -1;
    }
    uuid_result = UuidCreate(&gate->sublayer_key);
    if (uuid_result != RPC_S_OK && uuid_result != RPC_S_UUID_LOCAL_ONLY) {
        (void)snprintf(error, error_capacity, "cannot create WFP sublayer identifier: %lu",
                       (unsigned long)uuid_result);
        gate_engine_close(gate);
        return -1;
    }
    memset(&sublayer, 0, sizeof(sublayer));
    sublayer.subLayerKey = gate->sublayer_key;
    sublayer.displayData.name = L"DutchOven dynamic gate";
    sublayer.displayData.description = L"Removed automatically when the session closes";
    /* Place the invocation-scoped sublayer at the top of the WFP sublayer weight range. */
    sublayer.weight = UINT16_MAX;
    result = FwpmSubLayerAdd0(gate->engine, &sublayer, NULL);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot add dynamic WFP sublayer: %lu",
                       (unsigned long)result);
        gate_engine_close(gate);
        return -1;
    }
    for (size_t index = 0; index < config->app_count; index++) {
        if (gate_prepare_app(config->apps[index], &gate->app_ids[index],
                             error, error_capacity) != 0) {
            gate_engine_close(gate);
            return -1;
        }
    }
    return 0;
}

static DWORD gate_add_filter(do_gate_engine *gate, FWP_BYTE_BLOB *app_id,
                             const GUID *layer_key, UINT64 *filter_id) {
    FWPM_FILTER_CONDITION0 condition;
    FWPM_FILTER0 filter;
    memset(&condition, 0, sizeof(condition));
    condition.fieldKey = do_condition_app_id;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    condition.conditionValue.byteBlob = app_id;
    memset(&filter, 0, sizeof(filter));
    filter.displayData.name = L"DutchOven timed application block";
    filter.displayData.description = L"Dynamic-session filter";
    filter.layerKey = *layer_key;
    filter.subLayerKey = gate->sublayer_key;
    filter.weight.type = FWP_UINT8;
    /* FWP_UINT8 filter weights are limited to the range 0..15. */
    filter.weight.uint8 = 15U;
    filter.numFilterConditions = 1U;
    filter.filterCondition = &condition;
    filter.action.type = FWP_ACTION_BLOCK;
    /* Prevent a lower-priority permit from overriding the terminal block decision. */
    filter.flags = FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT;
    return FwpmFilterAdd0(gate->engine, &filter, NULL, filter_id);
}

static int gate_block(do_gate_engine *gate, const do_gate_config *config,
                      char *error, size_t error_capacity) {
    /* One transaction makes all application and address-family filters visible atomically. */
    DWORD result = FwpmTransactionBegin0(gate->engine, 0U);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot begin WFP block transaction: %lu",
                       (unsigned long)result);
        return -1;
    }
    gate->filter_count = 0U;
    for (size_t index = 0; index < config->app_count; index++) {
        result = gate_add_filter(gate, gate->app_ids[index], &do_layer_connect_v4,
                                 &gate->filter_ids[gate->filter_count]);
        if (result != ERROR_SUCCESS) {
            goto abort_transaction;
        }
        gate->filter_count++;
        result = gate_add_filter(gate, gate->app_ids[index], &do_layer_connect_v6,
                                 &gate->filter_ids[gate->filter_count]);
        if (result != ERROR_SUCCESS) {
            goto abort_transaction;
        }
        gate->filter_count++;
    }
    result = FwpmTransactionCommit0(gate->engine);
    if (result != ERROR_SUCCESS) {
        gate->filter_count = 0U;
        (void)snprintf(error, error_capacity, "cannot commit WFP block transaction: %lu",
                       (unsigned long)result);
        return -1;
    }
    return 0;
abort_transaction:
    (void)FwpmTransactionAbort0(gate->engine);
    gate->filter_count = 0U;
    (void)snprintf(error, error_capacity, "cannot add WFP application filter: %lu",
                   (unsigned long)result);
    return -1;
}

static int gate_pass(do_gate_engine *gate, char *error, size_t error_capacity) {
    DWORD result;
    if (gate->filter_count == 0U) {
        return 0;
    }
    result = FwpmTransactionBegin0(gate->engine, 0U);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot begin WFP pass transaction: %lu",
                       (unsigned long)result);
        return -1;
    }
    for (size_t index = 0; index < gate->filter_count; index++) {
        result = FwpmFilterDeleteById0(gate->engine, gate->filter_ids[index]);
        /* Missing filters are already in the desired pass state and are safe to treat as success. */
        if (result != ERROR_SUCCESS && result != (DWORD)FWP_E_FILTER_NOT_FOUND) {
            (void)FwpmTransactionAbort0(gate->engine);
            (void)snprintf(error, error_capacity, "cannot remove WFP filter: %lu",
                           (unsigned long)result);
            return -1;
        }
    }
    result = FwpmTransactionCommit0(gate->engine);
    if (result != ERROR_SUCCESS) {
        (void)snprintf(error, error_capacity, "cannot commit WFP pass transaction: %lu",
                       (unsigned long)result);
        return -1;
    }
    gate->filter_count = 0U;
    return 0;
}

int do_gate_run(const do_gate_config *config, char *error, size_t error_capacity) {
    do_gate_engine gate;
    ULONGLONG run_start;
    ULONGLONG run_end;
    unsigned cycle = 0U;
    int outcome = -1;
    /* Dry-run intentionally stops before privilege checks or any BFE interaction. */
    if (config->dry_run != 0) {
        return 0;
    }
    if (!gate_is_administrator()) {
        (void)snprintf(error, error_capacity, "gate requires an elevated Administrator token");
        return -1;
    }
    if (gate_engine_open(&gate, config, error, error_capacity) != 0) {
        return -1;
    }
    (void)InterlockedExchange(&do_gate_stop_requested, 0L);
    (void)SetConsoleCtrlHandler(gate_console_handler, TRUE);
    if (config->warmup_ms > 0U) {
        printf("PASS warmup=%ums wall_ms=%llu\n", config->warmup_ms, gate_wall_ms());
        gate_flush_log();
        gate_wait_until(GetTickCount64() + (ULONGLONG)config->warmup_ms);
    }
    run_start = GetTickCount64();
    run_end = run_start + (ULONGLONG)config->duration_ms;
    /* A blackout needs one transaction and one deadline, avoiding redundant remove/add churn. */
    if (config->block_ms == config->period_ms) {
        cycle = 1U;
        if (gate_block(&gate, config, error, error_capacity) != 0) {
            goto cleanup;
        }
        printf("BLOCK cycle=1 filters=%llu wall_ms=%llu elapsed_ms=%llu\n",
               (unsigned long long)gate.filter_count, gate_wall_ms(),
               (unsigned long long)(GetTickCount64() - run_start));
        gate_flush_log();
        gate_wait_until(run_end);
        outcome = 0;
        goto cleanup;
    }
    while (!gate_stopping() && GetTickCount64() < run_end) {
        /* Absolute cycle deadlines prevent WFP and logging overhead from drifting the schedule. */
        ULONGLONG cycle_start = run_start + ((ULONGLONG)cycle * (ULONGLONG)config->period_ms);
        ULONGLONG block_end = cycle_start + (ULONGLONG)config->block_ms;
        ULONGLONG cycle_end = cycle_start + (ULONGLONG)config->period_ms;
        gate_wait_until(cycle_start);
        if (gate_stopping() || GetTickCount64() >= run_end) {
            break;
        }
        cycle++;
        if (gate_block(&gate, config, error, error_capacity) != 0) {
            goto cleanup;
        }
        printf("BLOCK cycle=%u filters=%llu wall_ms=%llu elapsed_ms=%llu\n", cycle,
               (unsigned long long)gate.filter_count, gate_wall_ms(),
               (unsigned long long)(GetTickCount64() - run_start));
        gate_flush_log();
        gate_wait_until(block_end < run_end ? block_end : run_end);
        if (gate_pass(&gate, error, error_capacity) != 0) {
            goto cleanup;
        }
        if (GetTickCount64() < run_end && !gate_stopping()) {
            printf("PASS cycle=%u wall_ms=%llu elapsed_ms=%llu\n", cycle,
                   gate_wall_ms(), (unsigned long long)(GetTickCount64() - run_start));
            gate_flush_log();
            gate_wait_until(cycle_end < run_end ? cycle_end : run_end);
        }
    }
    outcome = 0;
cleanup:
    /* Attempt explicit removal for observability; closing the dynamic session is the backstop. */
    if (outcome == 0 && gate_pass(&gate, error, error_capacity) != 0) {
        outcome = -1;
    } else if (outcome != 0) {
        char cleanup_error[DO_ERROR_MAX];
        (void)gate_pass(&gate, cleanup_error, sizeof(cleanup_error));
    }
    gate_engine_close(&gate);
    (void)SetConsoleCtrlHandler(gate_console_handler, FALSE);
    printf("CLEAN filters=0 session=closed cycles=%u wall_ms=%llu elapsed_ms=%llu\n",
           cycle, gate_wall_ms(), (unsigned long long)(GetTickCount64() - run_start));
    gate_flush_log();
    return outcome;
}

#else

int do_gate_run(const do_gate_config *config, char *error, size_t error_capacity) {
    /* The POSIX build exists for parser tests and configuration dry-runs only. */
    if (config->dry_run != 0) {
        return 0;
    }
    (void)snprintf(error, error_capacity, "the WFP gate requires a native Windows build");
    return -1;
}

#endif
