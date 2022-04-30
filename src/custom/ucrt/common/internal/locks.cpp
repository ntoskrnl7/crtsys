
#include <windows.h>

CRITICAL_SECTION CrtSyspLocaleLock;

_IRQL_requires_max_(APC_LEVEL)
_IRQL_requires_min_(PASSIVE_LEVEL)
EXTERN_C
NTSTATUS
CrtSyspInitializeLocaleLock (
	VOID
	)
{
	InitializeCriticalSection( &CrtSyspLocaleLock );
	return STATUS_SUCCESS;
}

EXTERN_C
VOID
CrtSyspUninitializeLocaleLock (
	VOID
	)
{
	DeleteCriticalSection( &CrtSyspLocaleLock );
}

_Acquires_lock_(_Global_critical_region_)
_Requires_lock_not_held_(CrtSyspLocaleLock)
_Acquires_lock_(CrtSyspLocaleLock)
_IRQL_requires_max_(APC_LEVEL)
_IRQL_requires_min_(PASSIVE_LEVEL)
extern "C" void __cdecl _lock_locales()
{
	EnterCriticalSection( &CrtSyspLocaleLock );
}

_Releases_lock_(_Global_critical_region_)
_Requires_lock_held_(CrtSyspLocaleLock)
_Releases_lock_(CrtSyspLocaleLock)
_IRQL_requires_max_(APC_LEVEL)
extern "C" void __cdecl _unlock_locales()
{
	LeaveCriticalSection( &CrtSyspLocaleLock );
}