#pragma once

#include <stddef.h>

#ifndef UNDNAME_32_BIT_DECODE
#define UNDNAME_32_BIT_DECODE 0x0800
#endif

#ifndef UNDNAME_TYPE_ONLY
#define UNDNAME_TYPE_ONLY 0x8000
#endif

extern "C" char* __cdecl __unDName(
    char* outputString,
    const char* name,
    int maxStringLength,
    void* (__cdecl* pAlloc)(size_t),
    void (__cdecl* pFree)(void*),
    unsigned short disableFlags);
