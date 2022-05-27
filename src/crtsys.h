// clang-format off

#pragma once

#include <ntdef.h>
typedef PSTRING PUTF8_STRING;

#ifndef DECLSPEC_RESTRICT
#if (_MSC_VER >= 1915) && !defined(MIDL_PASS)
#define DECLSPEC_RESTRICT   __declspec(restrict)
#else
#define DECLSPEC_RESTRICT
#endif
#endif

#undef _CTYPE_DISABLE_MACROS
#include <ctype.h>

#include <windows.h>

#pragma warning(disable : 4201)
#include <WinUser.h>

#define WINBASEAPI

EXTERN_C_START



// crt/src/stl/xdateord.cpp
WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return )
                               LPWSTR lpLCData,
                           _In_ int cchData);



// crt\src\stl\xgetwctype.cpp
WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType);



// ucrt/internal/winapi_thunks.cpp
WINBASEAPI
int
WINAPI
GetLocaleInfoW(
    _In_ LCID     Locale,
    _In_ LCTYPE   LCType,
    _Out_writes_opt_(cchData) LPWSTR lpLCData,
    _In_ int      cchData);



// ucrt/internal/winapi_thunks.cpp
WINBASEAPI
int
WINAPI
GetDateFormatW(
    _In_ LCID Locale,
    _In_ DWORD dwFlags,
    _In_opt_ CONST SYSTEMTIME* lpDate,
    _In_opt_ LPCWSTR lpFormat,
    _Out_writes_opt_(cchDate) LPWSTR lpDateStr,
    _In_ int cchDate
    );



// ucrt/internal/winapi_thunks.cpp
WINBASEAPI
int
WINAPI
GetTimeFormatW(
    _In_ LCID Locale,
    _In_ DWORD dwFlags,
    _In_opt_ CONST SYSTEMTIME* lpTime,
    _In_opt_ LPCWSTR lpFormat,
    _Out_writes_opt_(cchTime) LPWSTR lpTimeStr,
    _In_ int cchTime
    );



// ucrt\locale\getqloc_downlevel.cpp
typedef BOOL (CALLBACK* LOCALE_ENUMPROCW)(LPWSTR);                                          // DEPRECATED: please use LOCALE_ENUMPROCEX

WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesW(
    _In_ LOCALE_ENUMPROCW lpLocaleEnumProc,
    _In_ DWORD            dwFlags);



// ucrt/inc/corecrt_internal.h
typedef BOOL (CALLBACK* LOCALE_ENUMPROCEX)(LPWSTR, DWORD, LPARAM);

// ucrt/internal/winapi_thunks.cpp
WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesEx(
    _In_ LOCALE_ENUMPROCEX lpLocaleEnumProcEx,
    _In_ DWORD dwFlags,
    _In_ LPARAM lParam,
    _In_opt_ LPVOID lpReserved
);



// ucrt/inc/corecrt_internal.h
WINBASEAPI
BOOL
WINAPI
FindClose(
    _Inout_ HANDLE hFindFile
    );

// ucrt\inc\corecrt_internal_lowio.h
typedef struct INPUT_RECORD *PINPUT_RECORD;



// ucrt\startup\thread.cpp
WINBASEAPI
DWORD
WINAPI
ResumeThread(
    _In_ HANDLE hThread
    );

EXTERN_C_END
