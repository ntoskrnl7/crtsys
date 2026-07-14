// clang-format off

#include <windows.h>
#include <fenv.h>

#if defined(_ARM64_)
#include <intrin.h>
#endif

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

#if defined(_ARM64_)
	// ARM64 fenv state lives in FPCR/FPSR. XSTATE_MASK_LEGACY is zero on
	// current WDKs and XSTATE_SAVE is reserved for ARM64 extended features.
	return STATUS_SUCCESS;
#else
	CrtSyspFlsXStateSaveIndex = FlsAlloc( CrtySyspCleanupXState );
	if (CrtSyspFlsXStateSaveIndex == FLS_OUT_OF_INDEXES) {
		return STATUS_UNSUCCESSFUL;
	}
	return STATUS_SUCCESS;
#endif
}

#if !defined(_ARM64_)
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
#endif


#if defined(_ARM64_)

#define CRTSYS_ARM64_FENV_DENORMAL_MASK 0x20u
#define CRTSYS_ARM64_FENV_FLUSH_MASK    0xC00u

#define CRTSYS_ARM64_FPCR_IOE        0x00000100ull
#define CRTSYS_ARM64_FPCR_DZE        0x00000200ull
#define CRTSYS_ARM64_FPCR_OFE        0x00000400ull
#define CRTSYS_ARM64_FPCR_UFE        0x00000800ull
#define CRTSYS_ARM64_FPCR_IXE        0x00001000ull
#define CRTSYS_ARM64_FPCR_IDE        0x00008000ull
#define CRTSYS_ARM64_FPCR_RMODE_MASK 0x00C00000ull
#define CRTSYS_ARM64_FPCR_RMODE_RN   0x00000000ull
#define CRTSYS_ARM64_FPCR_RMODE_RP   0x00400000ull
#define CRTSYS_ARM64_FPCR_RMODE_RM   0x00800000ull
#define CRTSYS_ARM64_FPCR_RMODE_RZ   0x00C00000ull
#define CRTSYS_ARM64_FPCR_FZ         0x01000000ull
#define CRTSYS_ARM64_FPCR_DN         0x02000000ull

#define CRTSYS_ARM64_FPSR_IOC 0x00000001ull
#define CRTSYS_ARM64_FPSR_DZC 0x00000002ull
#define CRTSYS_ARM64_FPSR_OFC 0x00000004ull
#define CRTSYS_ARM64_FPSR_UFC 0x00000008ull
#define CRTSYS_ARM64_FPSR_IXC 0x00000010ull
#define CRTSYS_ARM64_FPSR_IDC 0x00000080ull

static unsigned long
CrtSyspArm64GetFenvControl (
	VOID
	)
{
	ULONG64 const Fpcr = (ULONG64)_ReadStatusReg( ARM64_FPCR );
	unsigned long Control = 0;

	if ((Fpcr & CRTSYS_ARM64_FPCR_IXE) == 0) Control |= FE_INEXACT;
	if ((Fpcr & CRTSYS_ARM64_FPCR_UFE) == 0) Control |= FE_UNDERFLOW;
	if ((Fpcr & CRTSYS_ARM64_FPCR_OFE) == 0) Control |= FE_OVERFLOW;
	if ((Fpcr & CRTSYS_ARM64_FPCR_DZE) == 0) Control |= FE_DIVBYZERO;
	if ((Fpcr & CRTSYS_ARM64_FPCR_IOE) == 0) Control |= FE_INVALID;
	if ((Fpcr & CRTSYS_ARM64_FPCR_IDE) == 0) Control |= CRTSYS_ARM64_FENV_DENORMAL_MASK;
	if ((Fpcr & CRTSYS_ARM64_FPCR_FZ) != 0) Control |= 0x400u;

	switch (Fpcr & CRTSYS_ARM64_FPCR_RMODE_MASK) {
	case CRTSYS_ARM64_FPCR_RMODE_RP:
		Control |= FE_UPWARD;
		break;
	case CRTSYS_ARM64_FPCR_RMODE_RM:
		Control |= FE_DOWNWARD;
		break;
	case CRTSYS_ARM64_FPCR_RMODE_RZ:
		Control |= FE_TOWARDZERO;
		break;
	default:
		Control |= FE_TONEAREST;
		break;
	}

	return Control;
}

static unsigned long
CrtSyspArm64GetFenvStatus (
	VOID
	)
{
	ULONG64 const Fpsr = (ULONG64)_ReadStatusReg( ARM64_FPSR );
	unsigned long Status = 0;

	if ((Fpsr & CRTSYS_ARM64_FPSR_IXC) != 0) Status |= FE_INEXACT;
	if ((Fpsr & CRTSYS_ARM64_FPSR_UFC) != 0) Status |= FE_UNDERFLOW;
	if ((Fpsr & CRTSYS_ARM64_FPSR_OFC) != 0) Status |= FE_OVERFLOW;
	if ((Fpsr & CRTSYS_ARM64_FPSR_DZC) != 0) Status |= FE_DIVBYZERO;
	if ((Fpsr & CRTSYS_ARM64_FPSR_IOC) != 0) Status |= FE_INVALID;
	if ((Fpsr & CRTSYS_ARM64_FPSR_IDC) != 0) Status |= CRTSYS_ARM64_FENV_DENORMAL_MASK;

	return Status;
}

static VOID
CrtSyspArm64SetFenvControl (
	_In_ unsigned long Control
	)
{
	ULONG64 Fpcr = (ULONG64)_ReadStatusReg( ARM64_FPCR );

	Fpcr &= ~(ULONG64)(CRTSYS_ARM64_FPCR_IDE | CRTSYS_ARM64_FPCR_IXE |
		CRTSYS_ARM64_FPCR_UFE | CRTSYS_ARM64_FPCR_OFE |
		CRTSYS_ARM64_FPCR_DZE | CRTSYS_ARM64_FPCR_IOE |
		CRTSYS_ARM64_FPCR_RMODE_MASK | CRTSYS_ARM64_FPCR_FZ |
		CRTSYS_ARM64_FPCR_DN);

	if ((Control & FE_INEXACT) == 0) Fpcr |= CRTSYS_ARM64_FPCR_IXE;
	if ((Control & FE_UNDERFLOW) == 0) Fpcr |= CRTSYS_ARM64_FPCR_UFE;
	if ((Control & FE_OVERFLOW) == 0) Fpcr |= CRTSYS_ARM64_FPCR_OFE;
	if ((Control & FE_DIVBYZERO) == 0) Fpcr |= CRTSYS_ARM64_FPCR_DZE;
	if ((Control & FE_INVALID) == 0) Fpcr |= CRTSYS_ARM64_FPCR_IOE;
	if ((Control & CRTSYS_ARM64_FENV_DENORMAL_MASK) == 0) Fpcr |= CRTSYS_ARM64_FPCR_IDE;
	if ((Control & CRTSYS_ARM64_FENV_FLUSH_MASK) != 0) Fpcr |= CRTSYS_ARM64_FPCR_FZ;

	switch (Control & FE_ROUND_MASK) {
	case FE_UPWARD:
		Fpcr |= CRTSYS_ARM64_FPCR_RMODE_RP;
		break;
	case FE_DOWNWARD:
		Fpcr |= CRTSYS_ARM64_FPCR_RMODE_RM;
		break;
	case FE_TOWARDZERO:
		Fpcr |= CRTSYS_ARM64_FPCR_RMODE_RZ;
		break;
	default:
		Fpcr |= CRTSYS_ARM64_FPCR_RMODE_RN;
		break;
	}

	_WriteStatusReg( ARM64_FPCR, (__int64)Fpcr );
}

static VOID
CrtSyspArm64SetFenvStatus (
	_In_ unsigned long Status
	)
{
	ULONG64 Fpsr = (ULONG64)_ReadStatusReg( ARM64_FPSR );

	Fpsr &= ~(ULONG64)(CRTSYS_ARM64_FPSR_IDC | CRTSYS_ARM64_FPSR_IXC |
		CRTSYS_ARM64_FPSR_UFC | CRTSYS_ARM64_FPSR_OFC |
		CRTSYS_ARM64_FPSR_DZC | CRTSYS_ARM64_FPSR_IOC);
	if ((Status & FE_INEXACT) != 0) Fpsr |= CRTSYS_ARM64_FPSR_IXC;
	if ((Status & FE_UNDERFLOW) != 0) Fpsr |= CRTSYS_ARM64_FPSR_UFC;
	if ((Status & FE_OVERFLOW) != 0) Fpsr |= CRTSYS_ARM64_FPSR_OFC;
	if ((Status & FE_DIVBYZERO) != 0) Fpsr |= CRTSYS_ARM64_FPSR_DZC;
	if ((Status & FE_INVALID) != 0) Fpsr |= CRTSYS_ARM64_FPSR_IOC;
	if ((Status & CRTSYS_ARM64_FENV_DENORMAL_MASK) != 0) Fpsr |= CRTSYS_ARM64_FPSR_IDC;

	_WriteStatusReg( ARM64_FPSR, (__int64)Fpsr );
}

#endif


_ACRTIMP int __cdecl fegetenv(_Out_ fenv_t *_Env)
{
#if defined(_ARM64_)
	_Env->_Fe_ctl = CrtSyspArm64GetFenvControl();
	_Env->_Fe_stat = CrtSyspArm64GetFenvStatus();
#else
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
#endif
   return 0;
}

_ACRTIMP int __cdecl fesetenv(_In_ fenv_t const *_Env)
{
#if defined(_ARM64_)
	fenv_t Actual;

	CrtSyspArm64SetFenvControl( _Env->_Fe_ctl );
	CrtSyspArm64SetFenvStatus( _Env->_Fe_stat );
	if (fegetenv( &Actual ) != 0) {
		return 1;
	}
	return Actual._Fe_ctl != _Env->_Fe_ctl ||
		Actual._Fe_stat != _Env->_Fe_stat;
#else
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
#endif
}

_ACRTIMP _Success_(return == 0) int __cdecl feholdexcept(_Out_ fenv_t *_Env)
{
#if defined(_ARM64_)
	fenv_t Held;

	if (fegetenv( _Env ) != 0) {
		return 1;
	}
	Held = *_Env;
	Held._Fe_ctl |= FE_ALL_EXCEPT;
	if (fesetenv( &Held ) != 0) {
		return 1;
	}
	CrtSyspArm64SetFenvStatus( 0 );
	return 0;
#else
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
#endif
}

_ACRTIMP int __cdecl fegetround(void)
{
#if defined(_ARM64_)
	return (int)(CrtSyspArm64GetFenvControl() & FE_ROUND_MASK);
#else
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
#endif
}
