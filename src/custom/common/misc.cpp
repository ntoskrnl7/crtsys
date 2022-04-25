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
	UNREFERENCED_PARAMETER(initType);
    KdBreakPoint(); // untested :-( 
    return S_OK;
}



#if !CRTSYS_USE_LIBCNTPR

//
//
//

#include <stdio.h>
EXTERN_C_START
_ACRTIMP_ALT FILE* __cdecl __acrt_iob_func(unsigned _Ix)
{
    KdBreakPoint();
    _Ix;
    return NULL;
}

void __cdecl abort() {
}
EXTERN_C_END
#endif



//
//
//

#if CRTSYS_USE_NTL_MAIN
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

    KdBreakPoint();

    return CrtSysDriverEntry( DriverObject,
                              RegistryPath );
}
#endif