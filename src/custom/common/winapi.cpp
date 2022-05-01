// clang-format off

#include <windows.h>

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
HANDLE
WINAPI
GetStdHandle(_In_ DWORD nStdHandle) {
  KdBreakPoint();
  return NULL;
}

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
ResumeThread(_In_ HANDLE hThread) {
  KdBreakPoint();
  return 0;
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
EXTERN_C_END