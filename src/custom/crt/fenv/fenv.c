// clang-format off

#include <windows.h>
#include <fenv.h>

NTSTATUS
CrtSysInitializeFlsXState (
	VOID
	);

VOID
NTAPI
CrtySyspCleanupXState (
	_In_ PVOID lpFlsData
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysInitializeFlsXState)
#pragma alloc_text(PAGE, CrtySyspCleanupXState)
#endif

DWORD CrtSyspFlsXStateSaveIndex;

VOID
NTAPI
CrtySyspCleanupXState (
	_In_ PVOID lpFlsData
	)
{
	PAGED_CODE();

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
	PAGED_CODE();

	CrtSyspFlsXStateSaveIndex = FlsAlloc( CrtySyspCleanupXState );
	if (CrtSyspFlsXStateSaveIndex == FLS_OUT_OF_INDEXES) {
		return STATUS_UNSUCCESSFUL;
	}
	return STATUS_SUCCESS;
}

PXSTATE_SAVE
CrtSyspSaveXState (
	VOID
	)
{
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave == NULL) {
#pragma warning(disable:4996)
		StateSave = ExAllocatePoolWithTag( NonPagedPool,
									   	   sizeof(XSTATE_SAVE),
									       'ttsX' );
#pragma warning(default:4996)
		if (StateSave == NULL) {
			return NULL;
		}
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			CrtySyspCleanupXState( StateSave );
			return NULL;
		}
		if (! FlsSetValue( CrtSyspFlsXStateSaveIndex,
						   StateSave )) {
			CrtySyspCleanupXState( StateSave );
			return NULL;
		}
#if defined(_ARM_) || defined(_ARM64_)
	} else if (StateSave->Dummy == 0) {
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			return NULL;
		}
	}
#else
	} else if (StateSave->XStateContext.Area == NULL) {
		if (! NT_SUCCESS(KeSaveExtendedProcessorState( XSTATE_MASK_LEGACY,
													   StateSave ))) {
			return NULL;
		}
	}
#endif
	return StateSave;
}

VOID
CrtSyspRestoreXState (
	_In_ PXSTATE_SAVE StateSave
	)
{
	if (StateSave == NULL) {
		return;
	}
	KeRestoreExtendedProcessorState( StateSave );
	RtlZeroMemory( StateSave,
				   sizeof(XSTATE_SAVE) );
}



_ACRTIMP int __cdecl fegetenv(_Out_ fenv_t *_Env)
{
	PXSTATE_SAVE StateSave = CrtSyspSaveXState();
#if defined(_X86_) || defined(_AMD64_)
	if (StateSave == NULL || StateSave->XStateContext.Area == NULL) {
		return 1;
	}
	_Env->_Fe_ctl = StateSave->XStateContext.Area->LegacyState.ControlWord;
	_Env->_Fe_stat = StateSave->XStateContext.Area->LegacyState.StatusWord;
	CrtSyspRestoreXState( StateSave );
#else
	if (StateSave == NULL) {
		return 1;
	}
	_Env->_Fe_stat = StateSave->Dummy;
	CrtSyspRestoreXState( StateSave );
#endif
   return 0;
}

_ACRTIMP int __cdecl fesetenv(_In_ fenv_t const *_Env)
{
	PXSTATE_SAVE StateSave = CrtSyspSaveXState();
#if defined(_X86_) || defined(_AMD64_)
	if (StateSave == NULL || StateSave->XStateContext.Area == NULL) {
		return 1;
	}
	StateSave->XStateContext.Area->LegacyState.ControlWord = (USHORT)_Env->_Fe_ctl;
	StateSave->XStateContext.Area->LegacyState.StatusWord = (USHORT)_Env->_Fe_stat;
#else
	if (StateSave == NULL) {
		return 1;
	}
	StateSave->Dummy = (USHORT)_Env->_Fe_stat;
#endif
	CrtSyspRestoreXState( StateSave );
	return 0;
}

_ACRTIMP _Success_(return == 0) int __cdecl feholdexcept(_Out_ fenv_t *_Env)
{
	PXSTATE_SAVE StateSave = CrtSyspSaveXState();
#if defined(_X86_) || defined(_AMD64_)
	if (StateSave == NULL || StateSave->XStateContext.Area == NULL) {
		return 1;
	}
	_Env->_Fe_ctl = StateSave->XStateContext.Area->LegacyState.ControlWord;
	_Env->_Fe_stat = StateSave->XStateContext.Area->LegacyState.StatusWord;

	StateSave->XStateContext.Area->LegacyState.StatusWord &= ~FE_ALL_EXCEPT;
#else
	if (StateSave == NULL) {
		return 1;
	}
	_Env->_Fe_stat = StateSave->Dummy;
	StateSave->Dummy &= ~FE_ALL_EXCEPT;
#endif
	CrtSyspRestoreXState( StateSave );
	return CrtSyspSaveXState() == NULL ? 0 : 1;
}

_ACRTIMP int __cdecl fegetround(void)
{
#if defined(_X86_) || defined(_AMD64_)
	PXSTATE_SAVE StateSave = (PXSTATE_SAVE)FlsGetValue( CrtSyspFlsXStateSaveIndex );
	if (StateSave && StateSave->XStateContext.Area) {
		return (int)StateSave->XStateContext.Area->LegacyState.ControlWord & _MCW_RC;
	}
#endif
	unsigned int state;
	if (_controlfp_s(&state, 0, 0)) {
		return -1;
	}
	return (int)state & _MCW_RC;
}