#ifndef _CRTSYS_WINNLS_
#define _CRTSYS_WINNLS_

#include <Ldk/winnls.h>

#include "datetimeapi.h"
#include "stringapiset.h"

EXTERN_C_START

// crt/src/stl/xdateord.cpp
WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return )
                               LPWSTR lpLCData,
                           _In_ int cchData);



// ucrt/internal/winapi_thunks.cpp
WINBASEAPI
int
WINAPI
GetLocaleInfoW(
    _In_ LCID     Locale,
    _In_ LCTYPE   LCType,
    _Out_writes_opt_(cchData) LPWSTR lpLCData,
    _In_ int      cchData);


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

EXTERN_C_END

#endif // _CRTSYS_WINNLS_
