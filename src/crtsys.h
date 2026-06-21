// clang-format off

#pragma once

// sdkddkver.h may be included before ntddk.h(ntdef.h), which can leave
// DECLSPEC_DEPRECATED_DDK, DECLSPEC_DEPRECATED_DDK_WINXP, and related macros
// undefined. Include ntdef.h here first.

#include <ntdef.h>

#ifndef CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS
#define CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS 1
#endif

#if CRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS
#define CRTSYS_DIAGNOSTIC_BREAK() KdBreakPoint()
#else
#define CRTSYS_DIAGNOSTIC_BREAK() ((void)0)
#endif
