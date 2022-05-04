// clang-format off

#include <windows.h>

#define WINBASEAPI

#pragma warning(disable : 4100)

EXTERN_C_START

// 14.31.31103\crt\src\stl\xdateord.cpp
WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return) LPWSTR lpLCData,
                           _In_ int cchData) {
  KdBreakPoint();
  return 0;
}

// 14.31.31103\crt\src\stl\xgetwctype.cpp
WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType) {
  KdBreakPoint();
  return FALSE;
}

//
// for google test
//

WINBASEAPI
LPSTR
WINAPI
GetCommandLineA(VOID) {
    KdBreakPoint();
    return NULL;
}

WINBASEAPI
DWORD
WINAPI
GetTempPathA(_In_ DWORD nBufferLength,
             _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer) {
  KdBreakPoint();
  return 0;
}

WINBASEAPI
UINT WINAPI GetTempFileNameA(_In_ LPCSTR lpPathName, _In_ LPCSTR lpPrefixString,
                             _In_ UINT uUnique,
                             _Out_writes_(MAX_PATH) LPSTR lpTempFileName) {
  KdBreakPoint();
  return 0;
}

WINBASEAPI
VOID WINAPI DebugBreak(VOID) { DbgBreakPoint(); }

WINBASEAPI
BOOL WINAPI CreatePipe(_Out_ PHANDLE hReadPipe, _Out_ PHANDLE hWritePipe,
                       _In_opt_ LPSECURITY_ATTRIBUTES lpPipeAttributes,
                       _In_ DWORD nSize) {
  return FALSE;
}

WINBASEAPI
UINT WINAPI SetErrorMode(_In_ UINT uMode) {
  // untested :-(
  return 0;
}

WINBASEAPI
BOOL WINAPI GetExitCodeProcess(_In_ HANDLE hProcess, _Out_ LPDWORD lpExitCode) {
  return FALSE;
}

WINBASEAPI
_Ret_maybenull_ HANDLE WINAPI OpenThread(_In_ DWORD dwDesiredAccess,
                                         _In_ BOOL bInheritHandle,
                                         _In_ DWORD dwThreadId) {
  KdBreakPoint();
  return NULL;
}

WINBASEAPI
DECLSPEC_NORETURN
VOID
WINAPI
ExitThread(
    _In_ DWORD dwExitCode
    )
{
}

WINBASEAPI
DECLSPEC_NORETURN
VOID
WINAPI
FreeLibraryAndExitThread(
    _In_ HMODULE hLibModule,
    _In_ DWORD dwExitCode
    )
{
}

WINBASEAPI
BOOL WINAPI SetThreadPriority(_In_ HANDLE hThread, _In_ int nPriority) {
  return FALSE;
}

WINBASEAPI
int WINAPI GetThreadPriority(_In_ HANDLE hThread) {
  KdBreakPoint();
  return 0;
}

WINBASEAPI
DWORD
WINAPI
ResumeThread(
    _In_ HANDLE hThread
    )
{
    KdBreakPoint();
    return 0;
}

WINBASEAPI
VOID
WINAPI
GetStartupInfoW(
    _Out_ LPSTARTUPINFOW lpStartupInfo
    )
{
  RtlZeroMemory(lpStartupInfo, sizeof(STARTUPINFOW));
  lpStartupInfo->cb = sizeof(STARTUPINFOW);
}

WINBASEAPI
BOOL WINAPI CreateProcessA(
    _In_opt_ LPCSTR lpApplicationName, _Inout_opt_ LPSTR lpCommandLine,
    _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
    _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
    _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags,
    _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCSTR lpCurrentDirectory,
    _In_ struct STARTUPINFOA *lpStartupInfo,
    _Out_ struct PROCESS_INFORMATION *lpProcessInformation) {
  return FALSE;
}

WINBASEAPI
HANDLE
WINAPI
OpenProcess(_In_ DWORD dwDesiredAccess, _In_ BOOL bInheritHandle,
            _In_ DWORD dwProcessId) {
  KdBreakPoint();
  return NULL;
}

WINBASEAPI
BOOL WINAPI GetConsoleScreenBufferInfo(
    _In_ HANDLE hConsoleOutput,
    _Out_ struct CONSOLE_SCREEN_BUFFER_INFO *lpConsoleScreenBufferInfo) {
  return FALSE;
}

WINBASEAPI
BOOL WINAPI SetConsoleTextAttribute(_In_ HANDLE hConsoleOutput,
                                    _In_ WORD wAttributes) {
  return FALSE;
}




//
// ucrt
//

WINBASEAPI
BOOL
WINAPI
SetEnvironmentVariableW(
    _In_ LPCWSTR lpName,
    _In_opt_ LPCWSTR lpValue
    )
{
  return FALSE;
}

WINBASEAPI
_NullNull_terminated_
LPWCH
WINAPI
GetEnvironmentStringsW(
    VOID
    )
{
  return L"\0\0";
}

WINBASEAPI
BOOL
WINAPI
FreeEnvironmentStringsW(
    _In_ _Pre_ _NullNull_terminated_ LPWCH penv
    )
{
  return FALSE;
}

// WINBASEAPI
// BOOL
// WINAPI
// GetFileSizeEx(
//     _In_ HANDLE hFile,
//     _Out_ PLARGE_INTEGER lpFileSize
//     )
// {
//     return FALSE;
// }

__callback
WINBASEAPI
LONG
WINAPI
UnhandledExceptionFilter(
    _In_ struct _EXCEPTION_POINTERS* ExceptionInfo
    )
{
    return 0;
}

WINBASEAPI
LPTOP_LEVEL_EXCEPTION_FILTER
WINAPI
SetUnhandledExceptionFilter(
    _In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter
    )
{
    return NULL;
}

// extern "C" HANDLE CrtSyspStdInHandle = NULL;
// extern "C" HANDLE CrtSyspStdOutHandle = NULL;
// extern "C" HANDLE CrtSyspStdErrorHandle = NULL;

// WINBASEAPI
// HANDLE
// WINAPI
// GetStdHandle(
//     _In_ DWORD nStdHandle
//     )
// {
//   switch (nStdHandle) {
//   case STD_INPUT_HANDLE:
//     return CrtSyspStdInHandle;
//     break;
//   case STD_OUTPUT_HANDLE:
//     return CrtSyspStdOutHandle;
//     break;
//   case STD_ERROR_HANDLE:
//     return CrtSyspStdErrorHandle;
//     break;
//   }
//   return INVALID_HANDLE_VALUE;
// }

// WINBASEAPI
// BOOL
// WINAPI
// SetStdHandle(
//     _In_ DWORD nStdHandle,
//     _In_ HANDLE hHandle
//     )
// {
//     return FALSE;
// }



// WINBASEAPI
// UINT
// WINAPI
// GetOEMCP(void)
// {
//   return CP_ACP;
// }

// WINBASEAPI
// UINT
// WINAPI
// GetACP(void)
// {
//   return CP_ACP;
// }

// WINBASEAPI
// BOOL
// WINAPI
// IsValidCodePage(
//     _In_ UINT  CodePage)
// {
//     KdBreakPoint();
//     return TRUE;
// }

// WINBASEAPI
// BOOL
// WINAPI
// GetCPInfo(
//     _In_ UINT       CodePage,
//     _Out_ LPCPINFO  lpCPInfo)
// {
//   return FALSE;
// }



// WINBASEAPI
// UINT
// WINAPI
// GetConsoleOutputCP(
//     VOID
//     )
// {
//   return CP_ACP;
// }

// WINBASEAPI
// BOOL
// WINAPI
// GetConsoleMode(
//     _In_ HANDLE hConsoleHandle,
//     _Out_ LPDWORD lpMode
//     )
// {
//   return FALSE;
// }

// WINBASEAPI
// _Success_(return != FALSE)
// BOOL
// WINAPI
// ReadConsoleW(
//     _In_ HANDLE hConsoleInput,
//     _Out_writes_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR%),*lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
//     _In_ DWORD nNumberOfCharsToRead,
//     _Out_ _Deref_out_range_(<=,nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
//     _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
//     )
// {
//   return FALSE;
// }

// WINBASEAPI
// BOOL
// WINAPI
// WriteConsoleW(
//     _In_ HANDLE hConsoleOutput,
//     _In_reads_(nNumberOfCharsToWrite) CONST VOID* lpBuffer,
//     _In_ DWORD nNumberOfCharsToWrite,
//     _Out_opt_ LPDWORD lpNumberOfCharsWritten,
//     _Reserved_ LPVOID lpReserved
//     )
// {
//   return FALSE;
// }


//
// datetimeapi.h
//
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
    )
{
  return 0;
}

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
    )
{
    return 0;
}


// WINBASEAPI
// BOOL
// WINAPI
// IsValidLocale(
//     _In_ LCID   Locale,
//     _In_ DWORD  dwFlags)
// {
//   return FALSE;
// }

// WINBASEAPI
// int
// WINAPI
// LCIDToLocaleName(
//     _In_ LCID     Locale,
//     _Out_writes_opt_(cchName) LPWSTR  lpName,
//     _In_ int      cchName,
//     _In_ DWORD    dwFlags)
// {
//     return 0;
// }

// WINBASEAPI
// LCID
// WINAPI
// LocaleNameToLCID(
//     _In_ LPCWSTR lpName,
//     _In_ DWORD dwFlags)
// {
//   return 0;
// }

WINBASEAPI
int
WINAPI
GetLocaleInfoW(
    _In_ LCID     Locale,
    _In_ LCTYPE   LCType,
    _Out_writes_opt_(cchData) LPWSTR lpLCData,
    _In_ int      cchData)
{
  return 0;
}

WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesW(
    _In_ LOCALE_ENUMPROCW lpLocaleEnumProc,
    _In_ DWORD            dwFlags)
{
 return FALSE; 
}

WINBASEAPI
LCID
WINAPI
GetUserDefaultLCID(void)
{
  return 0;
}
WINBASEAPI
int
WINAPI
GetUserDefaultLocaleName(
    _Out_writes_(cchLocaleName) LPWSTR lpLocaleName,
    _In_ int cchLocaleName
    )
{
    return 0;
}


WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesEx(
    _In_ LOCALE_ENUMPROCEX lpLocaleEnumProcEx,
    _In_ DWORD dwFlags,
    _In_ LPARAM lParam,
    _In_opt_ LPVOID lpReserved
  )
{
  return FALSE;
}

//
// timezoneapi.h
//

WINBASEAPI
_Success_(return != TIME_ZONE_ID_INVALID)
DWORD
WINAPI
GetTimeZoneInformation(
    _Out_ LPTIME_ZONE_INFORMATION lpTimeZoneInformation
    )
{
    return 0;
}

#ifdef _M_IX86
void __cdecl _crt_debugger_hook(int)
#else
void __cdecl __crt_debugger_hook(int)
#endif
{
    KdBreakPoint();
}
EXTERN_C_END