// clang-format off

#include <corecrt_terminate.h>
#include <ehdata.h>

#if defined _M_AMD64 || defined _M_ARM || defined _M_ARM64
EXTERN_C
EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler_noexcept (
    _In_ struct _EXCEPTION_RECORD* ExceptionRecord,
    _In_ void* EstablisherFrame,
    _Inout_ struct _CONTEXT* ContextRecord,
    _Inout_ struct _DISPATCHER_CONTEXT* DispatcherContext
    )
{
    KdBreakPoint(); // untested :-( 

	EXCEPTION_DISPOSITION Excption = __C_specific_handler( ExceptionRecord,
                                                           EstablisherFrame,
                                                           ContextRecord,
                                                           DispatcherContext);
	if (IS_DISPATCHING(ExceptionRecord->ExceptionFlags) && ExceptionRecord->ExceptionCode == EH_EXCEPTION_NUMBER && Excption == ExceptionContinueSearch) {
		terminate();
	}
	return Excption;
}
#endif