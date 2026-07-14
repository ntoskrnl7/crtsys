// clang-format off

//
// vcruntime/guard_support.c
//

extern "C" unsigned char _guard_xfg_dispatch_icall_nop = 0x90;



//
// vcruntime/utility_app.cpp
//

#include <windows.h>

#if defined(_WIN64)
#define CRTSYS_DEFAULT_SECURITY_COOKIE ((UINT_PTR)0x00002B992DDFA232)
#else
#define CRTSYS_DEFAULT_SECURITY_COOKIE ((UINT_PTR)0xBB40E64E)
#endif

extern "C" DECLSPEC_SELECTANY UINT_PTR
    __security_cookie = CRTSYS_DEFAULT_SECURITY_COOKIE;
extern "C" DECLSPEC_SELECTANY UINT_PTR
    __security_cookie_complement = ~CRTSYS_DEFAULT_SECURITY_COOKIE;

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
    // libcntpr.lib!__ptlocinfo->lc_time_curr is initialized to NULL, so set
    // __lc_time_curr explicitly.
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
extern "C" void __cdecl __CxxRaiseException(
    _In_ DWORD dwExceptionCode,
    _In_ DWORD dwExceptionFlags,
    _In_ DWORD nNumberOfArguments,
    _In_reads_opt_(nNumberOfArguments) CONST ULONG_PTR* lpArguments
    )
{
    RaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
}

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

extern "C" _ACRTIMP void __cdecl _cexit(void)
{
    _do_onexit();
}

extern "C" bool __cdecl __scrt_uninitialize_crt(bool const is_terminating, bool const from_exit)
{
    ASSERT(from_exit == false);
    UNREFERENCED_PARAMETER(from_exit);

    __acrt_uninitialize(is_terminating);
    __vcrt_uninitialize(is_terminating);

    return true;
}
#endif
