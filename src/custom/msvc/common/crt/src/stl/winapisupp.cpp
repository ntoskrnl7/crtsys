#include <process.h>
#include <stdlib.h>
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

int __cdecl __crtLCMapStringA(LPCWSTR LocaleName, DWORD dwMapFlags,
                              LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr,
                              int cchDest, int code_page, BOOL bError) {
  if (lpSrcStr == nullptr || cchSrc < -1 || cchDest < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  const DWORD mb_flags = bError ? MB_ERR_INVALID_CHARS : 0;
  const int wide_source_count =
      MultiByteToWideChar(code_page, mb_flags, lpSrcStr, cchSrc, nullptr, 0);
  if (wide_source_count == 0) {
    return 0;
  }

  wchar_t *const wide_source = static_cast<wchar_t *>(
      calloc(static_cast<size_t>(wide_source_count), sizeof(wchar_t)));
  if (wide_source == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }

  if (MultiByteToWideChar(code_page, mb_flags, lpSrcStr, cchSrc, wide_source,
                          wide_source_count) == 0) {
    free(wide_source);
    return 0;
  }

  const int source_count = cchSrc == -1 ? -1 : wide_source_count;
  if ((dwMapFlags & LCMAP_SORTKEY) != 0) {
    const int result =
        LCMapStringEx(LocaleName, dwMapFlags, wide_source, source_count,
                      reinterpret_cast<LPWSTR>(lpDestStr), cchDest, nullptr,
                      nullptr, 0);
    free(wide_source);
    return result;
  }

  const int wide_dest_count = LCMapStringEx(
      LocaleName, dwMapFlags, wide_source, source_count, nullptr, 0, nullptr,
      nullptr, 0);
  if (wide_dest_count == 0) {
    free(wide_source);
    return 0;
  }

  wchar_t *const wide_dest = static_cast<wchar_t *>(
      calloc(static_cast<size_t>(wide_dest_count), sizeof(wchar_t)));
  if (wide_dest == nullptr) {
    free(wide_source);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }

  if (LCMapStringEx(LocaleName, dwMapFlags, wide_source, source_count, wide_dest,
                    wide_dest_count, nullptr, nullptr, 0) == 0) {
    free(wide_dest);
    free(wide_source);
    return 0;
  }

  const DWORD wc_flags = bError ? WC_ERR_INVALID_CHARS : 0;
  const int result =
      WideCharToMultiByte(code_page, wc_flags, wide_dest, wide_dest_count,
                          lpDestStr, cchDest, nullptr, nullptr);

  free(wide_dest);
  free(wide_source);
  return result;
}

int __cdecl __crtLCMapStringW(LPCWSTR const locale_name, DWORD const map_flags,
                              LPCWSTR const source, int source_count,
                              LPWSTR const destination,
                              int const destination_count) {
  return LCMapStringEx(locale_name, map_flags, source, source_count, destination,
                       destination_count, nullptr, nullptr, 0);
}

int __cdecl __crtCompareStringA(LPCWSTR LocaleName, DWORD dwCmpFlags,
                                LPCSTR lpString1, int cchCount1,
                                LPCSTR lpString2, int cchCount2,
                                int code_page) {
  if (lpString1 == nullptr || lpString2 == nullptr || cchCount1 < -1 ||
      cchCount2 < -1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  const int wide_count1 =
      MultiByteToWideChar(code_page, 0, lpString1, cchCount1, nullptr, 0);
  if (wide_count1 == 0) {
    return 0;
  }

  const int wide_count2 =
      MultiByteToWideChar(code_page, 0, lpString2, cchCount2, nullptr, 0);
  if (wide_count2 == 0) {
    return 0;
  }

  wchar_t *const wide_string1 = static_cast<wchar_t *>(
      calloc(static_cast<size_t>(wide_count1), sizeof(wchar_t)));
  wchar_t *const wide_string2 = static_cast<wchar_t *>(
      calloc(static_cast<size_t>(wide_count2), sizeof(wchar_t)));
  if (wide_string1 == nullptr || wide_string2 == nullptr) {
    free(wide_string2);
    free(wide_string1);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }

  if (MultiByteToWideChar(code_page, 0, lpString1, cchCount1, wide_string1,
                          wide_count1) == 0 ||
      MultiByteToWideChar(code_page, 0, lpString2, cchCount2, wide_string2,
                          wide_count2) == 0) {
    free(wide_string2);
    free(wide_string1);
    return 0;
  }

  const int result = CompareStringEx(
      LocaleName, dwCmpFlags, wide_string1, cchCount1 == -1 ? -1 : wide_count1,
      wide_string2, cchCount2 == -1 ? -1 : wide_count2, nullptr, nullptr, 0);

  free(wide_string2);
  free(wide_string1);
  return result;
}

int __cdecl __crtCompareStringW(LPCWSTR LocaleName, DWORD dwCmpFlags,
                                LPCWSTR lpString1, int cchCount1,
                                LPCWSTR lpString2, int cchCount2) {
  return CompareStringEx(LocaleName, dwCmpFlags, lpString1, cchCount1,
                         lpString2, cchCount2, nullptr, nullptr, 0);
}

BOOL __cdecl __crtGetStringTypeA(_In_opt_ _locale_t _Plocinfo,
                                 _In_ DWORD _DWInfoType, _In_ LPCSTR _LpSrcStr,
                                 _In_ int _CchSrc, _Out_ LPWORD _LpCharType,
                                 _In_ int _Code_page, _In_ int _Lcid,
                                 _In_ BOOL _BError) {
  UNREFERENCED_PARAMETER(_Plocinfo);
  UNREFERENCED_PARAMETER(_Lcid);

  if (_LpSrcStr == nullptr || _LpCharType == nullptr || _CchSrc < -1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  const DWORD mb_flags = _BError ? MB_ERR_INVALID_CHARS : 0;
  const int wide_count =
      MultiByteToWideChar(_Code_page, mb_flags, _LpSrcStr, _CchSrc, nullptr, 0);
  if (wide_count == 0) {
    return FALSE;
  }

  wchar_t *const wide_source = static_cast<wchar_t *>(
      calloc(static_cast<size_t>(wide_count), sizeof(wchar_t)));
  WORD *const wide_types =
      static_cast<WORD *>(calloc(static_cast<size_t>(wide_count), sizeof(WORD)));
  if (wide_source == nullptr || wide_types == nullptr) {
    free(wide_types);
    free(wide_source);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }

  if (MultiByteToWideChar(_Code_page, mb_flags, _LpSrcStr, _CchSrc, wide_source,
                          wide_count) == 0) {
    free(wide_types);
    free(wide_source);
    return FALSE;
  }

  const int source_count = _CchSrc == -1 ? -1 : wide_count;
  const BOOL result =
      GetStringTypeW(_DWInfoType, wide_source, source_count, wide_types);
  if (result != FALSE) {
    const int copy_count = _CchSrc == -1 ? wide_count : _CchSrc;
    for (int index = 0; index < copy_count; ++index) {
      _LpCharType[index] = wide_types[index];
    }
  }

  free(wide_types);
  free(wide_source);
  return result;
}

EXTERN_C_END
