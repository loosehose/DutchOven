#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: check_symbols.sh <objdump> <object>" >&2
    exit 2
fi

objdump_bin=$1
object_file=$2
# Read the table once so entry-point and import validation inspect the same object state.
symbol_table=$($objdump_bin -t "$object_file")

if ! grep -Eq '[[:space:]]go$' <<<"$symbol_table"; then
    echo "BOF contract check failed: go entry point is missing" >&2
    exit 1
fi

# The allowlist prevents accidental CRT or compiler-helper imports from reaching Beacon.
while IFS= read -r symbol; do
    case "$symbol" in
        __imp_BeaconDataParse|\
        __imp_BeaconDataExtract|\
        __imp_BeaconDataLength|\
        __imp_BeaconDataInt|\
        __imp_BeaconPrintf|\
        __imp_BeaconIsAdmin|\
        __imp_KERNEL32\$GetFileAttributesW|\
        __imp_KERNEL32\$GetFullPathNameW|\
        __imp_KERNEL32\$GetLongPathNameW|\
        __imp_KERNEL32\$GetModuleFileNameW|\
        __imp_KERNEL32\$lstrcmpiW|\
        __imp_KERNEL32\$Sleep|\
        __imp_RPCRT4\$UuidCreate|\
        __imp_FWPUCLNT\$FwpmEngineOpen0|\
        __imp_FWPUCLNT\$FwpmEngineClose0|\
        __imp_FWPUCLNT\$FwpmGetAppIdFromFileName0|\
        __imp_FWPUCLNT\$FwpmFreeMemory0|\
        __imp_FWPUCLNT\$FwpmSubLayerAdd0|\
        __imp_FWPUCLNT\$FwpmTransactionBegin0|\
        __imp_FWPUCLNT\$FwpmTransactionCommit0|\
        __imp_FWPUCLNT\$FwpmTransactionAbort0|\
        __imp_FWPUCLNT\$FwpmFilterAdd0|\
        __imp_FWPUCLNT\$FwpmFilterDeleteById0)
            ;;
        *)
            echo "BOF contract check failed: unexpected import $symbol" >&2
            exit 1
            ;;
    esac
done < <(awk '/\(sec  0\)/ {print $NF}' <<<"$symbol_table")

echo "BOF contract check passed: go entry point and DFR-only imports"
