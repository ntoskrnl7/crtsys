#ifndef _CRTSYS_APISETSTRING_
#define _CRTSYS_APISETSTRING_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include "winnls.h"

EXTERN_C_START

// crt\src\stl\xgetwctype.cpp
WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType);

EXTERN_C_END

#endif // _CRTSYS_APISETSTRING_
