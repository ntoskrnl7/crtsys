// clang-format off

#include <wdm.h>
#include <corecrt_terminate.h>
#include <ehdata.h>

#if defined(_X86_)
EXTERN_C_START

int __cdecl
_except_handler3 (
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ struct _EXCEPTION_REGISTRATION *Registration,
    _In_ struct _CONTEXT *ContextRecord,
    _In_ struct _EXCEPTION_REGISTRATION *Dispatcher
    );

int __cdecl
_except_handler3_noexcept (
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ struct _EXCEPTION_REGISTRATION *Registration,
    _In_ struct _CONTEXT *ContextRecord,
    _In_ struct _EXCEPTION_REGISTRATION *Dispatcher
    )
{
    KdBreakPoint(); // untested :-( 

	int Excption = _except_handler3( ExceptionRecord,
                                     Registration,
                                     ContextRecord,
                                     Dispatcher );
	if (IS_DISPATCHING(ExceptionRecord->ExceptionFlags) && ExceptionRecord->ExceptionCode == EH_EXCEPTION_NUMBER && Excption == ExceptionContinueSearch) {
		terminate();
	}
	return Excption;
}

EXTERN_C_END
#endif