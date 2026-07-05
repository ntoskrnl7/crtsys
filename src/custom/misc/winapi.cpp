// clang-format off

#include <windows.h>

#define WINBASEAPI

#pragma warning(disable : 4100)

EXTERN_C_START

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
    CRTSYS_DIAGNOSTIC_BREAK();
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

//
// https://github.com/ntoskrnl7/crtsys/issues/23
//
// Windows SDK WRL corewrappers.h uses ReleaseMutex in MutexTraits::Unlock.
// MSVC STL ppltasks.cpp can reach WRL/corewrappers.h on _CRT_APP or
// UNDOCKED_WINDOWS_UCRT builds.
//
// MutexTraits::Lock, SemaphoreTraits::Lock
//
WINBASEAPI
BOOL
WINAPI
ReleaseMutex (
    _In_ HANDLE hMutex
    )
{
    CRTSYS_DIAGNOSTIC_BREAK();
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE; 
}

EXTERN_C_END
