// #include <windows.h>
#include <internal_shared.h>

#include <eh.h>
#include <process.h>

static long c_termination_complete = FALSE;

extern "C" extern _onexit_table_t __acrt_atexit_table;
extern "C" extern _onexit_table_t __acrt_at_quick_exit_table;

// thread_local atexit dtor handling. The APPCRT exports a function to set the
// callback function. The exe main function will call this to set the callback
// function so exit() can invoke destructors for thread-storage objects owned
// by the main thread.
static _tls_callback_type thread_local_exit_callback_func;


// CRT_REFACTOR TODO This needs to be declared somewhere more accessible and we
// need to clean up the static CRT exit coordination.
#if !defined CRTDLL && defined _DEBUG
    extern "C" bool __cdecl __scrt_uninitialize_crt(bool is_terminating, bool from_exit);
#endif



static int __cdecl atexit_exception_filter(unsigned long const _exception_code) throw()
{
    if (_exception_code == ('msc' | 0xE0000000))
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}



extern "C" void __cdecl __acrt_initialize_thread_local_exit_callback(void * encoded_null)
{
    thread_local_exit_callback_func = reinterpret_cast<_tls_callback_type>(encoded_null);
}

// Register the dynamic TLS dtor callback. Called from the vcstartup library
// as part of the EXE common_main to allow the acrt to call back into the scrt.
extern "C" void __cdecl _register_thread_local_exe_atexit_callback(_In_ _tls_callback_type const _Callback)
{
    // Can only set the callback once.
    if (thread_local_exit_callback_func != __crt_fast_encode_pointer(nullptr))
    {
        terminate();
    }

    thread_local_exit_callback_func = __crt_fast_encode_pointer(_Callback);
}



static void __cdecl common_exit(
    int                    const return_code,
    _crt_exit_cleanup_mode const cleanup_mode,
    _crt_exit_return_mode  const return_mode
    ) throw()
{
    // Run the C termination:
    bool crt_uninitialization_required = false;

    //__acrt_lock_and_call(__acrt_select_exit_lock(), [&]
    {
        static bool c_exit_complete = false;
        if (c_exit_complete)
        {
            return;
        }

        _InterlockedExchange(&c_termination_complete, TRUE);

        __try
        {
            if (cleanup_mode == _crt_exit_full_cleanup)
            {

                // If this module has any dynamically initialized
                // __declspec(thread) variables, then we invoke their
                // destruction for the primary thread. All thread_local
                // destructors are sequenced before any atexit calls or static
                // object destructors (3.6.3/1)
                if (thread_local_exit_callback_func != __crt_fast_encode_pointer(nullptr))
                {
                    (__crt_fast_decode_pointer(thread_local_exit_callback_func))(nullptr, DLL_PROCESS_DETACH, nullptr);
                }

                _execute_onexit_table(&__acrt_atexit_table);
            }
            else if (cleanup_mode == _crt_exit_quick_cleanup)
            {
                _execute_onexit_table(&__acrt_at_quick_exit_table);
            }
        }
        __except (atexit_exception_filter(GetExceptionCode()))
        {
            terminate();
        }

        #ifndef CRTDLL
        // When the CRT is statically linked, we are responsible for executing
        // the terminators here, because the CRT code is present in this module.
        // When the CRT DLLs are used, the terminators will be executed when
        // the CRT DLLs are unloaded, after the call to ExitProcess.
        if (cleanup_mode == _crt_exit_full_cleanup)
        {
            _initterm(__xp_a, __xp_z);
        }

        _initterm(__xt_a, __xt_z);
        #endif // CRTDLL

        if (return_mode == _crt_exit_terminate_process)
        {
            c_exit_complete = true;
            crt_uninitialization_required = true;
        }
    }
    //);

    // Do NOT try to uninitialize the CRT while holding one of its locks.
    if (crt_uninitialization_required)
    {
        // If we are about to terminate the process, if the debug CRT is linked
        // statically into this module and this module is an EXE, we need to
        // ensure that we fully and correctly uninitialize the CRT so that the
        // debug on-exit() checks (e.g. debug heap leak detection) have a chance
        // to run.
        //
        // We do not need to uninitialize the CRT when it is statically linked
        // into a DLL because its DllMain will be called for DLL_PROCESS_DETACH
        // and we can uninitialize the CRT there.
        //
        // We never need to uninitialize the retail CRT during exit() because
        // the process is about to terminate.
        #if !CRTDLL && _DEBUG
        __scrt_uninitialize_crt(true, true);
        #endif
    }

    if (return_mode == _crt_exit_terminate_process)
    {
        ExitProcess(return_code);
    }
}

extern "C" int __cdecl _is_c_termination_complete()
{
    return static_cast<int>(__crt_interlocked_read(&c_termination_complete));
}



extern "C" void __cdecl exit(int const return_code)
{
    common_exit(return_code, _crt_exit_full_cleanup, _crt_exit_terminate_process);
}

extern "C" void __cdecl _exit(int const return_code)
{
    common_exit(return_code, _crt_exit_no_cleanup, _crt_exit_terminate_process);
}

extern "C" void __cdecl _Exit(int const return_code)
{
    common_exit(return_code, _crt_exit_no_cleanup, _crt_exit_terminate_process);
}

extern "C" void __cdecl quick_exit(int const return_code)
{
    common_exit(return_code, _crt_exit_quick_cleanup, _crt_exit_terminate_process);
}

extern "C" void __cdecl _cexit()
{
    common_exit(0, _crt_exit_full_cleanup, _crt_exit_return_to_caller);
}

extern "C" void __cdecl _c_exit()
{
    common_exit(0, _crt_exit_no_cleanup, _crt_exit_return_to_caller);
}