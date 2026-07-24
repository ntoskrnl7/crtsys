#pragma once

#include "../../crtsys.h"

#include <Windows.h>

extern "C" void __cdecl CrtSysValidateFilesystemIrql() noexcept;

#if DBG
inline HMODULE
    WINAPI CrtSysFilesystemGetModuleHandleW(LPCWSTR module_name) noexcept {
  CrtSysValidateFilesystemIrql();
  return ::GetModuleHandleW(module_name);
}

inline FARPROC WINAPI CrtSysFilesystemGetProcAddress(HMODULE module,
                                                     LPCSTR name) noexcept {
  CrtSysValidateFilesystemIrql();
  return ::GetProcAddress(module, name);
}

inline DWORD WINAPI CrtSysFilesystemGetTempPathW(DWORD buffer_length,
                                                 LPWSTR buffer) noexcept {
  CrtSysValidateFilesystemIrql();
  return ::GetTempPathW(buffer_length, buffer);
}

#define AreFileApisANSI(...)                                                   \
  (CrtSysValidateFilesystemIrql(), AreFileApisANSI(__VA_ARGS__))
#define CloseHandle(...)                                                       \
  (CrtSysValidateFilesystemIrql(), CloseHandle(__VA_ARGS__))
#define CopyFile2(...) (CrtSysValidateFilesystemIrql(), CopyFile2(__VA_ARGS__))
#define CreateDirectoryExW(...)                                                \
  (CrtSysValidateFilesystemIrql(), CreateDirectoryExW(__VA_ARGS__))
#define CreateDirectoryW(...)                                                  \
  (CrtSysValidateFilesystemIrql(), CreateDirectoryW(__VA_ARGS__))
#define CreateFile2(...)                                                       \
  (CrtSysValidateFilesystemIrql(), CreateFile2(__VA_ARGS__))
#define CreateHardLinkW(...)                                                   \
  (CrtSysValidateFilesystemIrql(), CreateHardLinkW(__VA_ARGS__))
#define DeviceIoControl(...)                                                   \
  (CrtSysValidateFilesystemIrql(), DeviceIoControl(__VA_ARGS__))
#define FindClose(...) (CrtSysValidateFilesystemIrql(), FindClose(__VA_ARGS__))
#define FindFirstFileExW(...)                                                  \
  (CrtSysValidateFilesystemIrql(), FindFirstFileExW(__VA_ARGS__))
#define FindFirstFileW(...)                                                    \
  (CrtSysValidateFilesystemIrql(), FindFirstFileW(__VA_ARGS__))
#define FindNextFileW(...)                                                     \
  (CrtSysValidateFilesystemIrql(), FindNextFileW(__VA_ARGS__))
#define GetCurrentDirectoryW(...)                                              \
  (CrtSysValidateFilesystemIrql(), GetCurrentDirectoryW(__VA_ARGS__))
#define GetDiskFreeSpaceExW(...)                                               \
  (CrtSysValidateFilesystemIrql(), GetDiskFreeSpaceExW(__VA_ARGS__))
#define GetFileAttributesExW(...)                                              \
  (CrtSysValidateFilesystemIrql(), GetFileAttributesExW(__VA_ARGS__))
#define GetFileAttributesW(...)                                                \
  (CrtSysValidateFilesystemIrql(), GetFileAttributesW(__VA_ARGS__))
#define GetFileInformationByHandle(...)                                        \
  (CrtSysValidateFilesystemIrql(), GetFileInformationByHandle(__VA_ARGS__))
#define GetFileInformationByHandleEx(...)                                      \
  (CrtSysValidateFilesystemIrql(), GetFileInformationByHandleEx(__VA_ARGS__))
#define GetFinalPathNameByHandleW(...)                                         \
  (CrtSysValidateFilesystemIrql(), GetFinalPathNameByHandleW(__VA_ARGS__))
#define GetFullPathNameW(...)                                                  \
  (CrtSysValidateFilesystemIrql(), GetFullPathNameW(__VA_ARGS__))
#define GetModuleHandleW CrtSysFilesystemGetModuleHandleW
#define GetProcAddress CrtSysFilesystemGetProcAddress
#define GetTempPathW CrtSysFilesystemGetTempPathW
#define MoveFileExW(...)                                                       \
  (CrtSysValidateFilesystemIrql(), MoveFileExW(__VA_ARGS__))
#define MultiByteToWideChar(...)                                               \
  (CrtSysValidateFilesystemIrql(), MultiByteToWideChar(__VA_ARGS__))
#define SetCurrentDirectoryW(...)                                              \
  (CrtSysValidateFilesystemIrql(), SetCurrentDirectoryW(__VA_ARGS__))
#define SetFileAttributesW(...)                                                \
  (CrtSysValidateFilesystemIrql(), SetFileAttributesW(__VA_ARGS__))
#define SetFileInformationByHandle(...)                                        \
  (CrtSysValidateFilesystemIrql(), SetFileInformationByHandle(__VA_ARGS__))
#define SetFileTime(...)                                                       \
  (CrtSysValidateFilesystemIrql(), SetFileTime(__VA_ARGS__))
#define WideCharToMultiByte(...)                                               \
  (CrtSysValidateFilesystemIrql(), WideCharToMultiByte(__VA_ARGS__))
#endif
