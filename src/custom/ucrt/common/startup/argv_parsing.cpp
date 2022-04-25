#include <vcstartup_internal.h>

EXTERN_C _ACRTIMP errno_t __cdecl _configure_narrow_argv(
    _In_ _crt_argv_mode mode
    )
{
    mode;
    KdBreakPoint();
    return 0;
}