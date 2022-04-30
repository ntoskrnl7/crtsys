#include <vcstartup_internal.h>

EXTERN_C int __cdecl _seh_filter_dll (
    unsigned long const xcptnum,
    PEXCEPTION_POINTERS const pxcptinfoptrs
    )
{
    if (xcptnum != ('msc' | 0xE0000000))
        return EXCEPTION_CONTINUE_SEARCH;

    KdBreakPoint();
    pxcptinfoptrs;
    return EXCEPTION_CONTINUE_EXECUTION;
}