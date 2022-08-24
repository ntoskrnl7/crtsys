#ifndef _CRTSYS_SYNCHAPI_H_
#define _CRTSYS_SYNCHAPI_H_

#pragma once

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

EXTERN_C_START

WINBASEAPI
BOOL
WINAPI
ReleaseSemaphore (
    _In_ HANDLE hSemaphore,
    _In_ LONG lReleaseCount,
    _Out_opt_ LPLONG lpPreviousCount
    );

WINBASEAPI
BOOL
WINAPI
ReleaseMutex (
    _In_ HANDLE hMutex
    );

EXTERN_C_END

#include <../Ldk/synchapi.h>

#endif // _CRTSYS_SYNCHAPI_H_