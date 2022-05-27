#define _CORECRT_BUILD
#define _CRT_GLOBAL_STATE_ISOLATION
#define _CRT_DECLARE_GLOBAL_VARIABLES_DIRECTLY
#include <corecrt_internal.h>

extern "C" errno_t __cdecl _configure_narrow_argv(_crt_argv_mode const mode)
{
    mode;
    return 0;
}