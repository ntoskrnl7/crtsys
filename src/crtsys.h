// clang-format off

#pragma once

// sdkddkver.h may be included before ntddk.h(ntdef.h), which can leave
// DECLSPEC_DEPRECATED_DDK, DECLSPEC_DEPRECATED_DDK_WINXP, and related macros
// undefined. Include ntdef.h here first.

#include <ntdef.h>

#if defined(_M_ARM64) && defined(_MSC_VER) && _MSC_VER < 1930
//
// WDK 10.0.26100.0 wdm.h can reference _CountOneBits64 before the v142 ARM64
// intrinsic declaration is visible. Keep this narrow compatibility declaration
// ahead of any later WDK header include.
//
#ifdef __cplusplus
extern "C" {
#endif
unsigned int __cdecl _CountOneBits64(unsigned __int64);
#ifdef __cplusplus
}
#endif
#endif

#ifndef CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS
#define CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS 1
#endif

#if CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS
#define CRTSYS_DIAGNOSTIC_BREAK() KdBreakPoint()
#else
#define CRTSYS_DIAGNOSTIC_BREAK() ((void)0)
#endif
