// clang-format off

#include <wdm.h>
#include <corecrt_terminate.h>
#include <ehdata.h>

#if defined(_X86_)
EXTERN_C_START

EXCEPTION_DISPOSITION __cdecl
_except_handler4(
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Inout_ PVOID DispatcherContext
    );

EXCEPTION_DISPOSITION __cdecl
_except_handler4_noexcept(
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Inout_ PVOID DispatcherContext
    )
{
    KdBreakPoint(); // untested :-( 

	EXCEPTION_DISPOSITION Excption = _except_handler4( ExceptionRecord,
                                                       EstablisherFrame,
                                                       ContextRecord,
                                                       DispatcherContext );
	if (IS_DISPATCHING(ExceptionRecord->ExceptionFlags) && ExceptionRecord->ExceptionCode == EH_EXCEPTION_NUMBER && Excption == ExceptionContinueSearch) {
		terminate();
	}
	return Excption;
}

EXTERN_C_END
#endif