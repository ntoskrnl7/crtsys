#pragma once

#if defined(_M_ARM64) && defined(_MSC_VER) && _MSC_VER < 1930
#ifdef __cplusplus
extern "C" {
#endif
unsigned int __cdecl _CountOneBits64(unsigned __int64);
#ifdef __cplusplus
}
#endif
#endif
