// clang-format off

#include <windows.h>

#pragma warning(disable : 4100)

// 14.31.31103\crt\src\stl\xdateord.cpp
WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return) LPWSTR lpLCData,
                           _In_ int cchData) {
  KdBreakPoint();
  return 0;
}

// 14.31.31103\crt\src\stl\xgetwctype.cpp
WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType) {
  KdBreakPoint();
  return FALSE;
}