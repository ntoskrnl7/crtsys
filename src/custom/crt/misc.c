#include <internal_shared.h>

//
// ucrt/misc/invalid_parameter.cpp (__acrt_call_reportfault -> _CRT_DEBUGGER_HOOK)
//
void __cdecl _CRT_DEBUGGER_HOOK(int reserved)
{
    reserved;
    KdBreakPoint();
}



//
// 10.0.17763.0
// 10.0.22000.0
// km\x86\libcntpr.lib(ieee87.obj)
//
#ifdef _M_IX86
void ** __cdecl __pxcptinfoptrs(void);
void ** __cdecl _pxcptinfoptrs(void)
{
    return __pxcptinfoptrs();
}

unsigned int __cdecl __get_fpsr_sse2()
{
    KdBreakPoint();
    return 0;
}

void __cdecl __set_fpsr_sse2(unsigned int _Arg)
{
    _Arg;
    KdBreakPoint();
}

#include <float.h>
void __cdecl _setdefaultprecision()
{
    KdBreakPoint();
    _controlfp_s(NULL, _PC_53, _MCW_PC);
}
#endif



//
// signbit
//
#include <math.h>

_Check_return_ _ACRTIMP int __cdecl _dsign(_In_ double _X)
{
    return _DSIGN_C(_X);
}

_Check_return_ _ACRTIMP int __cdecl _ldsign(_In_ long double _X)
{
    return _LSIGN_C(_X);
}
_Check_return_ _ACRTIMP int __cdecl _fdsign(_In_ float _X)
{
    return _FSIGN_C(_X);
}



//
//
//

#if UCXXRT
#include <stdlib.h>

int __do_unsigned_char_lconv_initialization = 255;

_ACRTIMP __declspec(noreturn) void __cdecl exit(_In_ int _Code) { _exit(_Code); }
_ACRTIMP __declspec(noreturn) void __cdecl _exit(_In_ int _Code) { _Code; }
_ACRTIMP __declspec(noreturn) void __cdecl abort(void) {}

//
// ucrt\startup\abort.cpp
//
#ifdef _DEBUG
    #define _INIT_ABORT_BEHAVIOR _WRITE_ABORT_MSG
#else
    #define _INIT_ABORT_BEHAVIOR _CALL_REPORTFAULT
#endif

unsigned int __abort_behavior = _INIT_ABORT_BEHAVIOR;

unsigned int __cdecl _set_abort_behavior(unsigned int flags, unsigned int mask)
{
    unsigned int oldflags = __abort_behavior;
    __abort_behavior = oldflags & (~mask) | flags & mask;
    return oldflags;
}
#endif