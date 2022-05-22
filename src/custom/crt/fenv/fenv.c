// clang-format off

#include <wdm.h>
#include <windows.h>
#include <fenv.h>

DWORD CrtSyspFlsXStateSaveIndex;

VOID
NTAPI
CrtySyspCleanupXState (
	_In_ PVOID lpFlsData
	)
{
	if (lpFlsData) {
		ExFreePoolWithTag( lpFlsData,
						   'ttsX' );
	}
}

NTSTATUS
CrtSysInitializeFlsXState (
	VOID
	)
{
	CrtSyspFlsXStateSaveIndex = FlsAlloc( NULL );
	if (CrtSyspFlsXStateSaveIndex == FLS_OUT_OF_INDEXES) {
		return STATUS_UNSUCCESSFUL;
	}
	return STATUS_SUCCESS;
}

_ACRTIMP int __cdecl fegetenv(_Out_ fenv_t *_Env)
{
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave == NULL) {
		StateSave = ExAllocatePoolWithTag( NonPagedPool,
									   	   sizeof(XSTATE_SAVE),
									       'ttsX' );
		if (StateSave == NULL) {
			return 1;
		}
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			CrtySyspCleanupXState( StateSave );
			return 1;
		}
		if (! FlsSetValue( CrtSyspFlsXStateSaveIndex,
						   StateSave )) {
			CrtySyspCleanupXState( StateSave );
			return 1;
		}
	} else if (StateSave->XStateContext.Area == NULL) {
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			return 1;
		}
	}

	if (StateSave->XStateContext.Area) {
		_Env->_Fe_ctl = StateSave->XStateContext.Area->LegacyState.ControlWord;
		_Env->_Fe_stat = StateSave->XStateContext.Area->LegacyState.StatusWord;
	}
    return 0;
}

_ACRTIMP int __cdecl fesetenv(_In_ fenv_t const *_Env)
{
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave == NULL) {
		return 1;
	}

	if (StateSave->XStateContext.Area == NULL) {
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			return 1;
		}
	}
	if (StateSave->XStateContext.Area) {
		StateSave->XStateContext.Area->LegacyState.ControlWord = (USHORT)_Env->_Fe_ctl;
		StateSave->XStateContext.Area->LegacyState.StatusWord = (USHORT)_Env->_Fe_stat;
	}
	KeRestoreExtendedProcessorState( StateSave );
	return 0;
}

_ACRTIMP int __cdecl feclearexcept(_In_ int _Flags)
{
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave == NULL) {
		return 1;
	}
	if (StateSave->XStateContext.Area) {
    	_Flags &= FE_ALL_EXCEPT;
		StateSave->XStateContext.Area->LegacyState.StatusWord &= ~_Flags;
	}
	KeRestoreExtendedProcessorState( StateSave );
	return 0;
}

_ACRTIMP int __cdecl fegetround(void)
{
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave == NULL) {
		return 1;
	}

	if (StateSave->XStateContext.Area) {
		return (int)StateSave->XStateContext.Area->LegacyState.ControlWord & _MCW_RC;
	}

	unsigned int state;
	if (_controlfp_s(&state, 0, 0)) {
		return -1;
	}
	return (int)state & _MCW_RC;
}