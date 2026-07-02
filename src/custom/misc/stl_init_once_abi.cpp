#include <Windows.h>

extern "C" {

using CrtSysStdInitOnceBeginInitializeFn =
    BOOL(WINAPI *)(LPINIT_ONCE, DWORD, PBOOL, LPVOID *);
using CrtSysStdInitOnceCompleteFn = BOOL(WINAPI *)(LPINIT_ONCE, DWORD, LPVOID);

#if defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM)
// Older MSVC STL headers can reference these ABI helpers as imported
// function pointers. LDK provides the underlying Win32 InitOnce functions as
// normal static-library symbols, so provide the import-pointer ABI symbols
// that the STL object code expects.
__declspec(selectany) CrtSysStdInitOnceBeginInitializeFn
    __imp___std_init_once_begin_initialize = InitOnceBeginInitialize;

__declspec(selectany) CrtSysStdInitOnceCompleteFn __imp___std_init_once_complete =
    InitOnceComplete;
#endif

}
