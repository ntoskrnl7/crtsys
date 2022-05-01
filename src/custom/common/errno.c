//
// https://github.com/SpoilerScriptsGroup/RetrievAL/blob/develop/SpoilerAL-winmm.dll/crt/errno.c
//
#ifdef _MSC_VER
#include <errno.h>
errno_t _terrno = 0;
errno_t *__cdecl _errno()
{
    return &_terrno;
}
#endif

#ifdef __BORLANDC__
#pragma warn - 8070
extern int *__errno();
__declspec(naked) int *__cdecl _errno()
{
    __asm
    {
		jmp     __errno
    }
}
#endif