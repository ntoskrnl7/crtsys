
#include <stdio.h>

extern "C" int __cdecl fputc(int const c, FILE* const stream)
{
    c;
    stream;
    KdBreakPoint();
    return -1;
}