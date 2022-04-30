#include <process.h>
#include <windows.h>

EXTERN_C_START

#if _STL_WIN32_WINNT < _WIN32_WINNT_WIN8
void __cdecl __crtGetSystemTimePreciseAsFileTime(
    _Out_ LPFILETIME lpSystemTimeAsFileTime) {
  GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}
#endif // _STL_WIN32_WINNT < _WIN32_WINNT_WIN8

BOOL __cdecl __crtQueueUserWorkItem(__in LPTHREAD_START_ROUTINE function,
                                    __in_opt PVOID context, __in ULONG flags) {
  return QueueUserWorkItem(function, context, flags);
}

#pragma warning(disable : 4100)

int __cdecl __crtLCMapStringA(LPCWSTR LocaleName, DWORD dwMapFlags,
                              LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr,
                              int cchDest, int code_page, BOOL bError) {
  KdBreakPoint();
  return 0;
}

int __cdecl __crtLCMapStringW(LPCWSTR const locale_name, DWORD const map_flags,
                              LPCWSTR const source, int source_count,
                              LPWSTR const destination,
                              int const destination_count) {
  KdBreakPoint();
  return 0;
}

int __cdecl __crtCompareStringA(LPCWSTR LocaleName, DWORD dwCmpFlags,
                                LPCSTR lpString1, int cchCount1,
                                LPCSTR lpString2, int cchCount2,
                                int code_page) {
  KdBreakPoint();
  return 0;
}

int __cdecl __crtCompareStringW(LPCWSTR LocaleName, DWORD dwCmpFlags,
                                LPCWSTR lpString1, int cchCount1,
                                LPCWSTR lpString2, int cchCount2) {
  KdBreakPoint();
  return 0;
}

BOOL __cdecl __crtGetStringTypeA(_In_opt_ _locale_t _Plocinfo,
                                 _In_ DWORD _DWInfoType, _In_ LPCSTR _LpSrcStr,
                                 _In_ int _CchSrc, _Out_ LPWORD _LpCharType,
                                 _In_ int _Code_page, _In_ int _Lcid,
                                 _In_ BOOL _BError) {
  KdBreakPoint();
  return FALSE;
}

EXTERN_C_END