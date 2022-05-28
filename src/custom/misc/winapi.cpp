// clang-format off

#include <windows.h>

#define WINBASEAPI

#pragma warning(disable : 4100)

EXTERN_C_START

//
// for google test
//

WINBASEAPI
DWORD
WINAPI
GetTempPathA (
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

WINBASEAPI
UINT
WINAPI
GetTempFileNameA (
    _In_ LPCSTR lpPathName, _In_ LPCSTR lpPrefixString,
    _In_ UINT uUnique,
    _Out_writes_(MAX_PATH) LPSTR lpTempFileName
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

WINBASEAPI
DWORD
WINAPI
ResumeThread (
    _In_ HANDLE hThread
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

WINBASEAPI
BOOL
WINAPI
CreateProcessA (
    _In_opt_ LPCSTR lpApplicationName,
    _Inout_opt_ LPSTR lpCommandLine,
    _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
    _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
    _In_ BOOL bInheritHandles,
    _In_ DWORD dwCreationFlags,
    _In_opt_ LPVOID lpEnvironment,
    _In_opt_ LPCSTR lpCurrentDirectory,
    _In_ struct STARTUPINFOA *lpStartupInfo,
    _Out_ struct PROCESS_INFORMATION *lpProcessInformation
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

WINBASEAPI
BOOL
WINAPI
GetConsoleScreenBufferInfo (
    _In_ HANDLE hConsoleOutput,
    _Out_ struct CONSOLE_SCREEN_BUFFER_INFO *lpConsoleScreenBufferInfo
    )
{
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

WINBASEAPI
BOOL
WINAPI
SetConsoleTextAttribute (
    _In_ HANDLE hConsoleOutput,
    _In_ WORD wAttributes
    )
{
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}



// 14.31.31103\crt\src\stl\xdateord.cpp
WINBASEAPI
int
WINAPI
GetLocaleInfoEx (
    _In_opt_ LPCWSTR lpLocaleName,
    _In_ LCTYPE LCType,
    _Out_writes_to_opt_(cchData, return) LPWSTR lpLCData,
    _In_ int cchData
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

// 14.31.31103\crt\src\stl\xgetwctype.cpp
WINBASEAPI
BOOL
WINAPI
GetStringTypeW (
    _In_ DWORD dwInfoType,
    _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
    _In_ int cchSrc,
    _Out_ LPWORD lpCharType
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}



//
// ucrt/internal/winapi_thunks.cpp 
//

// __acrt_GetDateFormatEx -> GetDateFormatW
WINBASEAPI
int
WINAPI
GetDateFormatW (
    _In_ LCID Locale,
    _In_ DWORD dwFlags,
    _In_opt_ CONST SYSTEMTIME* lpDate,
    _In_opt_ LPCWSTR lpFormat,
    _Out_writes_opt_(cchDate) LPWSTR lpDateStr,
    _In_ int cchDate
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );  
    return 0;
}

// __acrt_GetTimeFormatEx -> GetTimeFormatW
WINBASEAPI
int
WINAPI
GetTimeFormatW (
    _In_ LCID Locale,
    _In_ DWORD dwFlags,
    _In_opt_ CONST SYSTEMTIME* lpTime,
    _In_opt_ LPCWSTR lpFormat,
    _Out_writes_opt_(cchTime) LPWSTR lpTimeStr,
    _In_ int cchTime
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

// __acrt_GetLocaleInfoEx -> GetLocaleInfoW
WINBASEAPI
int
WINAPI
GetLocaleInfoW (
    _In_ LCID     Locale,
    _In_ LCTYPE   LCType,
    _Out_writes_opt_(cchData) LPWSTR lpLCData,
    _In_ int      cchData
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return 0;
}

// __acrt_EnumSystemLocalesEx -> EnumSystemLocalesEx
WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesEx (
    _In_ LOCALE_ENUMPROCEX lpLocaleEnumProcEx,
    _In_ DWORD dwFlags,
    _In_ LPARAM lParam,
    _In_opt_ LPVOID lpReserved
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}

//
// ucrt/locale/getqloc_downloevel.cpp (GetLcidFromLangCountry, GetLcidFromLanguage, GetLcidFromCountry)
//
WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesW (
    _In_ LOCALE_ENUMPROCW lpLocaleEnumProc,
    _In_ DWORD dwFlags
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE; 
}

EXTERN_C_END