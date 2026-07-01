#pragma once

//
// WDK 10.0.26100.0 wdm.h uses _CountOneBits64 in ARM64 helper code before
// older v142 ARM64 toolsets have necessarily seen the intrinsic declaration.
// Force intrin.h first and provide the missing v142 declaration.
//
#include <intrin.h>

#if defined(_M_ARM64) && defined(_MSC_VER) && _MSC_VER < 1930
#ifdef __cplusplus
extern "C" {
#endif
unsigned int __cdecl _CountOneBits64(unsigned __int64);
#ifdef __cplusplus
}
#endif
#endif
