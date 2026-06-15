#ifndef _CRTSYS_WINBASE_
#define _CRTSYS_WINBASE_

#include <apisetcconv.h>
#include <minwinbase.h>

#include <Ldk/winbase.h>

#include "fileapi.h"
#include "processthreadsapi.h"

EXTERN_C_START

WINBASEAPI
BOOL
WINAPI
VerifyVersionInfoW(
    _Inout_ PVOID lpVersionInformation,
    _In_ DWORD dwTypeMask,
    _In_ DWORDLONG dwlConditionMask
    );

#ifndef VerifyVersionInfo
#define VerifyVersionInfo VerifyVersionInfoW
#endif

EXTERN_C_END


#define OFS_MAXPATHNAME 128
typedef struct _OFSTRUCT {
    BYTE cBytes;
    BYTE fFixedDisk;
    WORD nErrCode;
    WORD Reserved1;
    WORD Reserved2;
    CHAR szPathName[OFS_MAXPATHNAME];
} OFSTRUCT, *LPOFSTRUCT, *POFSTRUCT;

#endif // _CRTSYS_WINBASE_
