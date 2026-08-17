#ifndef DUTCHOVEN_BOF_DEFS_H
#define DUTCHOVEN_BOF_DEFS_H

#include <fwpmu.h>
#include <rpc.h>
#include <windows.h>

/* MODULE$Function names opt each Win32 call into Beacon's Dynamic Function Resolution. */
DECLSPEC_IMPORT DWORD WINAPI KERNEL32$GetFileAttributesW(LPCWSTR);
DECLSPEC_IMPORT DWORD WINAPI KERNEL32$GetFullPathNameW(LPCWSTR, DWORD, LPWSTR, LPWSTR *);
DECLSPEC_IMPORT DWORD WINAPI KERNEL32$GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
DECLSPEC_IMPORT int WINAPI KERNEL32$lstrcmpiW(LPCWSTR, LPCWSTR);
DECLSPEC_IMPORT VOID WINAPI KERNEL32$Sleep(DWORD);

DECLSPEC_IMPORT RPC_STATUS RPC_ENTRY RPCRT4$UuidCreate(UUID *);

DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmEngineOpen0(
    const wchar_t *, UINT32, SEC_WINNT_AUTH_IDENTITY_W *, const FWPM_SESSION0 *, HANDLE *);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmEngineClose0(HANDLE);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmGetAppIdFromFileName0(
    const wchar_t *, FWP_BYTE_BLOB **);
DECLSPEC_IMPORT void WINAPI FWPUCLNT$FwpmFreeMemory0(void **);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmSubLayerAdd0(
    HANDLE, const FWPM_SUBLAYER0 *, PSECURITY_DESCRIPTOR);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmTransactionBegin0(HANDLE, UINT32);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmTransactionCommit0(HANDLE);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmTransactionAbort0(HANDLE);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmFilterAdd0(
    HANDLE, const FWPM_FILTER0 *, PSECURITY_DESCRIPTOR, UINT64 *);
DECLSPEC_IMPORT DWORD WINAPI FWPUCLNT$FwpmFilterDeleteById0(HANDLE, UINT64);

/* Preserve normal Win32 call sites while emitting the DFR symbol names above. */
#define GetFileAttributesW KERNEL32$GetFileAttributesW
#define GetFullPathNameW KERNEL32$GetFullPathNameW
#define GetModuleFileNameW KERNEL32$GetModuleFileNameW
#define lstrcmpiW KERNEL32$lstrcmpiW
#define Sleep KERNEL32$Sleep
#define UuidCreate RPCRT4$UuidCreate
#define FwpmEngineOpen0 FWPUCLNT$FwpmEngineOpen0
#define FwpmEngineClose0 FWPUCLNT$FwpmEngineClose0
#define FwpmGetAppIdFromFileName0 FWPUCLNT$FwpmGetAppIdFromFileName0
#define FwpmFreeMemory0 FWPUCLNT$FwpmFreeMemory0
#define FwpmSubLayerAdd0 FWPUCLNT$FwpmSubLayerAdd0
#define FwpmTransactionBegin0 FWPUCLNT$FwpmTransactionBegin0
#define FwpmTransactionCommit0 FWPUCLNT$FwpmTransactionCommit0
#define FwpmTransactionAbort0 FWPUCLNT$FwpmTransactionAbort0
#define FwpmFilterAdd0 FWPUCLNT$FwpmFilterAdd0
#define FwpmFilterDeleteById0 FWPUCLNT$FwpmFilterDeleteById0

#endif
