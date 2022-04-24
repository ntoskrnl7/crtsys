#include <stdio.h>

extern "C" int __cdecl fputs(char const* const string, FILE* const stream)
{
    string;
    stream;
    KdBreakPoint();
    return 0;
}