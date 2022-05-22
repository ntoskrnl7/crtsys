// clang-format off

#include <wdm.h>

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
    KdBreakPoint(); // untested :-( 
	UNREFERENCED_PARAMETER(initType);
    return S_OK;
}



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

#if CRTSYS_USE_LIBCNTPR
#include "../crt/setlocal.h"

#pragma warning(disable:4100)

#include <windows.h>

EXTERN_C
NTSTATUS
CrtSyspInitializeForLibcntpr (
    VOID
    )
{
    //
    // 10.0.17763.0
    // libcntpr.lib!__ptlocinfo->lc_time_curr가 NULL로 초기화되어있기 때문에 __lc_time_curr를 직접 설정해야합니다.
    // 
    __ptlocinfo->lc_time_curr = &__lc_time_c;

    return STATUS_SUCCESS;
}

EXTERN_C
VOID
CrtSyspUninitializeForLibcntpr (
    VOID
    )
{
}

#endif // CRTSYS_USE_LIBCNTPR

#if UCXXRT
extern "C" int  __cdecl _do_onexit();
extern "C" int  __cdecl _do_quick_onexit();
extern "C" void __cdecl __initialize_memory();



//
// ucrt\startup\onexit.cpp
//
// ucrt\internal\initialization.cpp initialize_c -> __acrt_atexit_table
//
#include <corecrt_startup.h>

extern "C" _onexit_table_t __acrt_atexit_table{};
extern "C" _onexit_table_t __acrt_at_quick_exit_table{};

extern "C" int __cdecl _initialize_onexit_table(_onexit_table_t* const table) { table; return 0; }

// ucrt\internal\initialization.cpp initialize_pointers -> __acrt_initialize_thread_local_exit_callback
extern "C" void __cdecl __acrt_initialize_thread_local_exit_callback(void * encoded_null) { encoded_null; }



//
// crt\src\vcruntime\initialization.cpp
//
extern "C" bool __cdecl __vcrt_initialize()
{
    return true;
}

// crt\src\vcruntime\initialization.cpp __acrt_thread_detach -> __acrt_thread_detach
extern "C" bool __cdecl __vcrt_uninitialize(bool const terminating)
{
    UNREFERENCED_PARAMETER(terminating);
    return true;
}



//
// crt\src\vcruntime\utility.cpp
//
#include <vcstartup_internal.h>

extern "C" __scrt_native_startup_state __scrt_current_native_startup_state  = __scrt_native_startup_state::uninitialized;

extern "C" bool __cdecl __scrt_initialize_onexit_tables(__scrt_module_type const module_type)
{
    module_type;
    return true;
}

extern "C" bool __cdecl __scrt_initialize_crt(__scrt_module_type const module_type)
{
    ASSERT(module_type == __scrt_module_type::exe);
    UNREFERENCED_PARAMETER(module_type);

    __isa_available_init();

    if (!__vcrt_initialize())
    {
        return false;
    }

    if (!__acrt_initialize())
    {
        __vcrt_uninitialize(false);
        return false;
    }

    // ucxxrt
    __initialize_memory();

    return true;
}

extern "C" bool __cdecl __scrt_uninitialize_crt(bool const is_terminating, bool const from_exit)
{
    ASSERT(from_exit == false);
    UNREFERENCED_PARAMETER(from_exit);

    // ucxxrt
    _do_onexit();

    __acrt_uninitialize(is_terminating);
    __vcrt_uninitialize(is_terminating);

    return true;
}
#endif