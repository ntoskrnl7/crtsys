#ifndef _CRTSYS_APISETFILE_
#define _CRTSYS_APISETFILE_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

EXTERN_C_START

// ucrt/inc/corecrt_internal.h
WINBASEAPI
BOOL
WINAPI
FindClose(
    _Inout_ HANDLE hFindFile
    );

EXTERN_C_END

#endif // _CRTSYS_APISETFILE_
