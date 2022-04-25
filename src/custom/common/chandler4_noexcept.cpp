#include <corecrt_terminate.h>
#include <ehdata.h>

#if defined(_X86_)
EXTERN_C_START
EXCEPTION_DISPOSITION __cdecl
_except_handler4(
    IN struct _EXCEPTION_RECORD *ExceptionRecord,
    IN PVOID EstablisherFrame,
    IN OUT struct _CONTEXT *ContextRecord,
    IN OUT PVOID DispatcherContext
    );

EXCEPTION_DISPOSITION __cdecl
_except_handler4_noexcept(
    IN struct _EXCEPTION_RECORD *ExceptionRecord,
    IN PVOID EstablisherFrame,
    IN OUT struct _CONTEXT *ContextRecord,
    IN OUT PVOID DispatcherContext
    )
{
    KdBreakPoint(); // untested :-( 

	EXCEPTION_DISPOSITION Excption = _except_handler4( ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
	if (IS_DISPATCHING(ExceptionRecord->ExceptionFlags) && ExceptionRecord->ExceptionCode == EH_EXCEPTION_NUMBER && Excption == ExceptionContinueSearch) {
		terminate();
	}
	return Excption;
}

EXTERN_C_END
#endif