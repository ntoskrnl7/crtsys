// clang-format off

//
// vcruntime/guard_support.c
//

extern "C" unsigned char _guard_xfg_dispatch_icall_nop = 0x90;



//
// vcruntime/utility_app.cpp
//

#include <windows.h>

#define _ROAPI_
#include <roapi.h>

EXTERN_C
ROAPI
_Check_return_
HRESULT
WINAPI
RoInitialize (
    _In_ RO_INIT_TYPE initType
    )
{
    // unreachable code
    KdBreakPoint(); // untested :-( 
	UNREFERENCED_PARAMETER(initType);
    return S_OK;
}



#if (!CRTSYS_USE_LIBCNTPR) || CRTSYS_NEED_CRT

//
//
//

#include <stdio.h>
EXTERN_C_START
_ACRTIMP_ALT FILE* __cdecl __acrt_iob_func(unsigned _Ix)
{
    // unreachable code
    KdBreakPoint();  // untested :-( 
    _Ix;
    return NULL;
}

void __cdecl abort() {
    KdBreakPoint();  // untested :-( 
}
EXTERN_C_END
#endif



#if CRTSYS_USE_NTL_MAIN
//
// 
// /GS 옵션을 사용하여 빌드하는 경우
// BufferOverflowK.lib(gs_support.obj) GsDriverEntry가 포함되며
// GsDriverEntry에서 DriverEntry를 호출하기 때문에 DriverEntry를 임의로 정의하였습니다.
//

EXTERN_C DRIVER_INITIALIZE DriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

//
// BufferOverflowK.lib(gs_support.obj)
//
// GsDriverEntry 
//
EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;

EXTERN_C
NTSTATUS
DriverEntry (
  _In_ PDRIVER_OBJECT DriverObject,
  _In_ PUNICODE_STRING RegistryPath
  )
{
    PAGED_CODE();

    //
    // ntl::main을 사용하도록 빌드하였는데 DriverEntry가 빌드되는건 뭔가 잘못된것입니다.
    // 
    KdBreakPoint();

    return CrtSysDriverEntry( DriverObject,
                              RegistryPath );
}
#endif