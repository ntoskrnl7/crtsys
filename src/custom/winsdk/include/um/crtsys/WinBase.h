#ifndef _CRTSYS_WINBASE_
#define _CRTSYS_WINBASE_

#include <apisetcconv.h>
#include <minwinbase.h>

#include <Ldk/winbase.h>

#include "fileapi.h"
#include "processthreadsapi.h"



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