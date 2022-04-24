#include <windows.h>
#include <process.h>

EXTERN_C_START

#if _STL_WIN32_WINNT < _WIN32_WINNT_WIN8
void __cdecl __crtGetSystemTimePreciseAsFileTime(_Out_ LPFILETIME lpSystemTimeAsFileTime) {
    GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}
#endif // _STL_WIN32_WINNT < _WIN32_WINNT_WIN8

#if !defined(UCXXRT)
BOOL __cdecl __crtQueueUserWorkItem(__in LPTHREAD_START_ROUTINE function, __in_opt PVOID context, __in ULONG flags)
{
    return QueueUserWorkItem(function, context, flags);
}
#endif
EXTERN_C_END