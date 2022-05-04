#pragma once

#undef _CTYPE_DISABLE_MACROS
#include <ctype.h>

#include <windows.h>

#pragma warning(disable : 4201)
#include <WinUser.h>

#define WINBASEAPI

EXTERN_C_START


typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[ 32 ];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[ 32 ];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

WINBASEAPI
_Success_(return != TIME_ZONE_ID_INVALID)
DWORD
WINAPI
GetTimeZoneInformation(
    _Out_ LPTIME_ZONE_INFORMATION lpTimeZoneInformation
    );




_ACRTIMP const unsigned short *__cdecl __pctype_func(void);
_ACRTIMP const wctype_t *__cdecl __pwctype_func(void);


WINBASEAPI
BOOL
WINAPI
GetCPInfo(
    _In_ UINT       CodePage,
    _Out_ LPCPINFO  lpCPInfo);

WINBASEAPI
UINT
WINAPI
GetOEMCP(void);

WINBASEAPI
UINT
WINAPI
GetACP(void);

WINBASEAPI
BOOL
WINAPI
IsValidCodePage(
    _In_ UINT  CodePage);



WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return )
                               LPWSTR lpLCData,
                           _In_ int cchData);

WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType);

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

WINBASEAPI
BOOL
WINAPI
IsValidLocale(
    _In_ LCID   Locale,
    _In_ DWORD  dwFlags);

WINBASEAPI
int
WINAPI
LCIDToLocaleName(
    _In_ LCID     Locale,
    _Out_writes_opt_(cchName) LPWSTR  lpName,
    _In_ int      cchName,
    _In_ DWORD    dwFlags);

WINBASEAPI
LCID
WINAPI
LocaleNameToLCID(
    _In_ LPCWSTR lpName,
    _In_ DWORD dwFlags);

WINBASEAPI
int
WINAPI
GetLocaleInfoW(
    _In_ LCID     Locale,
    _In_ LCTYPE   LCType,
    _Out_writes_opt_(cchData) LPWSTR lpLCData,
    _In_ int      cchData);

typedef BOOL (CALLBACK* LOCALE_ENUMPROCA)(LPSTR);                                           // Deprecated, please use Unicode
typedef BOOL (CALLBACK* LOCALE_ENUMPROCW)(LPWSTR);                                          // DEPRECATED: please use LOCALE_ENUMPROCEX

WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesW(
    _In_ LOCALE_ENUMPROCW lpLocaleEnumProc,
    _In_ DWORD            dwFlags);

typedef BOOL (CALLBACK* LOCALE_ENUMPROCEX)(LPWSTR, DWORD, LPARAM);

WINBASEAPI
BOOL
WINAPI
EnumSystemLocalesEx(
    _In_ LOCALE_ENUMPROCEX lpLocaleEnumProcEx,
    _In_ DWORD dwFlags,
    _In_ LPARAM lParam,
    _In_opt_ LPVOID lpReserved
);

WINBASEAPI
LCID
WINAPI
GetUserDefaultLCID(void);

WINBASEAPI
int
WINAPI
GetUserDefaultLocaleName(
    _Out_writes_(cchLocaleName) LPWSTR lpLocaleName,
    _In_ int cchLocaleName
    );

WINBASEAPI
BOOL
WINAPI
GetFileSizeEx(
    _In_ HANDLE hFile,
    _Out_ PLARGE_INTEGER lpFileSize
    );

WINBASEAPI
BOOL
WINAPI
FindClose(
    _Inout_ HANDLE hFindFile
    );

WINBASEAPI
DWORD
WINAPI
GetTempPathW(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength,return + 1) LPWSTR lpBuffer
    );

typedef BOOL (CALLBACK* LOCALE_ENUMPROCEX)(LPWSTR, DWORD, LPARAM);

#define STD_INPUT_HANDLE    ((DWORD)-10)
#define STD_OUTPUT_HANDLE   ((DWORD)-11)
#define STD_ERROR_HANDLE    ((DWORD)-12)

typedef struct INPUT_RECORD *PINPUT_RECORD;



typedef LONG (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(
    _In_ struct _EXCEPTION_POINTERS *ExceptionInfo
    );

typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;

__callback
WINBASEAPI
LONG
WINAPI
UnhandledExceptionFilter(
    _In_ struct _EXCEPTION_POINTERS* ExceptionInfo
    );

WINBASEAPI
LPTOP_LEVEL_EXCEPTION_FILTER
WINAPI
SetUnhandledExceptionFilter(
    _In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter
    );



WINBASEAPI
DWORD
WINAPI
ResumeThread(
    _In_ HANDLE hThread
    );

WINBASEAPI
DECLSPEC_NORETURN
VOID
WINAPI
ExitThread(
    _In_ DWORD dwExitCode
    );

WINBASEAPI
DECLSPEC_NORETURN
VOID
WINAPI
FreeLibraryAndExitThread(
    _In_ HMODULE hLibModule,
    _In_ DWORD dwExitCode
    );



typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _STARTUPINFOA {
    DWORD   cb;
    LPSTR   lpReserved;
    LPSTR   lpDesktop;
    LPSTR   lpTitle;
    DWORD   dwX;
    DWORD   dwY;
    DWORD   dwXSize;
    DWORD   dwYSize;
    DWORD   dwXCountChars;
    DWORD   dwYCountChars;
    DWORD   dwFillAttribute;
    DWORD   dwFlags;
    WORD    wShowWindow;
    WORD    cbReserved2;
    LPBYTE  lpReserved2;
    HANDLE  hStdInput;
    HANDLE  hStdOutput;
    HANDLE  hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
typedef struct _STARTUPINFOW {
    DWORD   cb;
    LPWSTR  lpReserved;
    LPWSTR  lpDesktop;
    LPWSTR  lpTitle;
    DWORD   dwX;
    DWORD   dwY;
    DWORD   dwXSize;
    DWORD   dwYSize;
    DWORD   dwXCountChars;
    DWORD   dwYCountChars;
    DWORD   dwFillAttribute;
    DWORD   dwFlags;
    WORD    wShowWindow;
    WORD    cbReserved2;
    LPBYTE  lpReserved2;
    HANDLE  hStdInput;
    HANDLE  hStdOutput;
    HANDLE  hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;
#ifdef UNICODE
typedef STARTUPINFOW STARTUPINFO;
typedef LPSTARTUPINFOW LPSTARTUPINFO;
#else
typedef STARTUPINFOA STARTUPINFO;
typedef LPSTARTUPINFOA LPSTARTUPINFO;
#endif // UNICODE

WINBASEAPI
VOID
WINAPI
GetStartupInfoW(
    _Out_ LPSTARTUPINFOW lpStartupInfo
    );



WINBASEAPI
BOOL
WINAPI
LockFileEx(
    _In_ HANDLE hFile,
    _In_ DWORD dwFlags,
    _Reserved_ DWORD dwReserved,
    _In_ DWORD nNumberOfBytesToLockLow,
    _In_ DWORD nNumberOfBytesToLockHigh,
    _Inout_ LPOVERLAPPED lpOverlapped
    );

WINBASEAPI
BOOL
WINAPI
UnlockFileEx(
    _In_ HANDLE hFile,
    _Reserved_ DWORD dwReserved,
    _In_ DWORD nNumberOfBytesToUnlockLow,
    _In_ DWORD nNumberOfBytesToUnlockHigh,
    _Inout_ LPOVERLAPPED lpOverlapped
    );


WINBASEAPI
BOOL
WINAPI
SetEnvironmentVariableW(
    _In_ LPCWSTR lpName,
    _In_opt_ LPCWSTR lpValue
    );

WINBASEAPI
_NullNull_terminated_
LPWCH
WINAPI
GetEnvironmentStringsW(
    VOID
    );

WINBASEAPI
BOOL
WINAPI
FreeEnvironmentStringsW(
    _In_ _Pre_ _NullNull_terminated_ LPWCH penv
    );



WINBASEAPI
HANDLE
WINAPI
GetStdHandle(
    _In_ DWORD nStdHandle
    );

WINBASEAPI
BOOL
WINAPI
SetStdHandle(
    _In_ DWORD nStdHandle,
    _In_ HANDLE hHandle
    );



WINBASEAPI
UINT
WINAPI
GetConsoleOutputCP(
    VOID
    );

WINBASEAPI
BOOL
WINAPI
GetConsoleMode(
    _In_ HANDLE hConsoleHandle,
    _Out_ LPDWORD lpMode
    );


WINBASEAPI
_Success_(return != FALSE)
BOOL
WINAPI
ReadConsoleW(
    _In_ HANDLE hConsoleInput,
    _Out_writes_bytes_to_(nNumberOfCharsToRead * sizeof(TCHAR%),*lpNumberOfCharsRead * sizeof(TCHAR%)) LPVOID lpBuffer,
    _In_ DWORD nNumberOfCharsToRead,
    _Out_ _Deref_out_range_(<=,nNumberOfCharsToRead) LPDWORD lpNumberOfCharsRead,
    _In_opt_ PCONSOLE_READCONSOLE_CONTROL pInputControl
    );

WINBASEAPI
BOOL
WINAPI
WriteConsoleW(
    _In_ HANDLE hConsoleOutput,
    _In_reads_(nNumberOfCharsToWrite) CONST VOID* lpBuffer,
    _In_ DWORD nNumberOfCharsToWrite,
    _Out_opt_ LPDWORD lpNumberOfCharsWritten,
    _Reserved_ LPVOID lpReserved
    );


//
// Ctrl Event flags
//

#define CTRL_C_EVENT        0
#define CTRL_BREAK_EVENT    1
#define CTRL_CLOSE_EVENT    2
// 3 is reserved!
// 4 is reserved!
#define CTRL_LOGOFF_EVENT   5
#define CTRL_SHUTDOWN_EVENT 6

//
// typedef for ctrl-c handler routines
//

typedef
BOOL
(WINAPI *PHANDLER_ROUTINE)(
    _In_ DWORD CtrlType
    );

WINBASEAPI
BOOL
WINAPI
SetConsoleCtrlHandler(
    _In_opt_ PHANDLER_ROUTINE HandlerRoutine,
    _In_ BOOL Add
    );
EXTERN_C_END