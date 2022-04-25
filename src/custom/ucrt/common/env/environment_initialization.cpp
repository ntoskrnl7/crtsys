//#include <corecrt_internal_traits.h>
// #include <stdlib.h>
// #include <string.h>

#include <windows.h>

// Windows Kits\10\Source\10.0.19041.0\ucrt\inc\corecrt_internal.h
// __crt_state_management::dual_state_global<int> *__p__fmode
//
// vs
// 
// Windows Kits\10\Include\10.0.19041.0\ucrt\stdlib.h
// _CRT_INSECURE_DEPRECATE_GLOBALS(_get_fmode  ) _ACRTIMP int*      __cdecl __p__fmode  (void);
// 

EXTERN_C int __cdecl _initialize_narrow_environment()
{
     KdBreakPoint();
     return 0;
    //return common_initialize_environment_nolock<char>();
}