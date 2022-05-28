#ifndef _CRTSYS_PROCESSTHREADSAPI_H_
#define _CRTSYS_PROCESSTHREADSAPI_H_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

EXTERN_C_START

// ucrt\startup\thread.cpp
WINBASEAPI
DWORD
WINAPI
ResumeThread(
    _In_ HANDLE hThread
    );

EXTERN_C_END

#endif // _CRTSYS_PROCESSTHREADSAPI_H_
