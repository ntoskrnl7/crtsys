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

//
// https://github.com/ntoskrnl7/crtsys/issues/23
//
// Windows Kits\10\Include\10.0.22621.0\winrt\wrl\wrappers\corewrappers.h
// Microsoft Visual Studio\2022\Preview\VC\Tools\MSVC\14.34.31721\crt\src\stl\ppltasks.cpp
//
// MutexTraits::Lock, SemaphoreTraits::Lock
//
WINBASEAPI
BOOL
WINAPI
ReleaseSemaphore (
    _In_ HANDLE hSemaphore,
    _In_ LONG lReleaseCount,
    _Out_opt_ LPLONG lpPreviousCount
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE; 
}

WINBASEAPI
BOOL
WINAPI
ReleaseMutex (
    _In_ HANDLE hMutex
    )
{
    KdBreakPoint();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE; 
}

#ifdef WINOLEAPI
#undef WINOLEAPI
#endif
#ifdef WINOLEAPI_
#undef WINOLEAPI_
#endif

#ifdef _OLE32_
#define WINOLEAPI        STDAPI
#define WINOLEAPI_(type) STDAPI_(type)
#else

#ifdef _68K_
#ifndef REQUIRESAPPLEPASCAL
#define WINOLEAPI        EXTERN_C HRESULT PASCAL
#define WINOLEAPI_(type) EXTERN_C type PASCAL
#else
#define WINOLEAPI        EXTERN_C PASCAL HRESULT
#define WINOLEAPI_(type) EXTERN_C PASCAL type
#endif
#else
#define WINOLEAPI        EXTERN_C HRESULT STDAPICALLTYPE
#define WINOLEAPI_(type) EXTERN_C type STDAPICALLTYPE
#endif

#endif

_Check_return_
WINOLEAPI
CoGetObjectContext(
    _In_ REFIID riid,
    _Outptr_ LPVOID  FAR * ppv
    )
{
    KdBreakPoint();
    return E_NOTIMPL;
}

typedef 
enum _APTTYPEQUALIFIER
    {
        APTTYPEQUALIFIER_NONE	= 0,
        APTTYPEQUALIFIER_IMPLICIT_MTA	= 1,
        APTTYPEQUALIFIER_NA_ON_MTA	= 2,
        APTTYPEQUALIFIER_NA_ON_STA	= 3,
        APTTYPEQUALIFIER_NA_ON_IMPLICIT_MTA	= 4,
        APTTYPEQUALIFIER_NA_ON_MAINSTA	= 5,
        APTTYPEQUALIFIER_APPLICATION_STA	= 6,
        APTTYPEQUALIFIER_RESERVED_1	= 7
    } 	APTTYPEQUALIFIER;

typedef 
enum _APTTYPE
    {
        APTTYPE_CURRENT	= -1,
        APTTYPE_STA	= 0,
        APTTYPE_MTA	= 1,
        APTTYPE_NA	= 2,
        APTTYPE_MAINSTA	= 3
    } 	APTTYPE;

_Check_return_
WINOLEAPI
CoGetApartmentType(
    _Out_ APTTYPE* pAptType,
    _Out_ APTTYPEQUALIFIER* pAptQualifier
    )
{
    KdBreakPoint();
    return E_NOTIMPL;
}

EXTERN_C_END
