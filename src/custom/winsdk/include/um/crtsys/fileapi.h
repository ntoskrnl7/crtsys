#ifndef _CRTSYS_APISETFILE_
#define _CRTSYS_APISETFILE_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

EXTERN_C_START

WINBASEAPI
DWORD
WINAPI
GetTempPathA(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength,return + 1) LPSTR lpBuffer
    );

WINBASEAPI
DWORD
WINAPI
GetTempPathW(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength,return + 1) LPWSTR lpBuffer
    );

WINBASEAPI
UINT
WINAPI
GetTempFileNameA(
    _In_ LPCSTR lpPathName,
    _In_ LPCSTR lpPrefixString,
    _In_ UINT uUnique,
    _Out_writes_(MAX_PATH) LPSTR lpTempFileName
    );

WINBASEAPI
UINT
WINAPI
GetTempFileNameW(
    _In_ LPCWSTR lpPathName,
    _In_ LPCWSTR lpPrefixString,
    _In_ UINT uUnique,
    _Out_writes_(MAX_PATH) LPWSTR lpTempFileName
    );

// ucrt/inc/corecrt_internal.h
WINBASEAPI
BOOL
WINAPI
FindClose(
    _Inout_ HANDLE hFindFile
    );

EXTERN_C_END

#endif // _CRTSYS_APISETFILE_
