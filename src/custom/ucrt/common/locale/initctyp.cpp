#include <windows.h>

//
// 모든 카테고리를 C locale인 것으로 처리되도록 합니다.
//

wchar_t *CrtSyspLocaleNames[] = {
    nullptr, // LC_ALL
    nullptr, // LC_COLLATE
    nullptr, // LC_CTYPE
    nullptr, // LC_MONETARY
    nullptr, // LC_NUMERIC
    nullptr  // LC_TIME
};

extern "C" int __cdecl ___mb_cur_max_func() {
  // untested :-(
  return 1;
}

extern "C" unsigned int __cdecl ___lc_codepage_func() {
  // untested :-(
  return CP_ACP;
}

extern "C" wchar_t **__cdecl ___lc_locale_name_func() {
  // untested :-(
  return CrtSyspLocaleNames;
}

extern "C" unsigned int __cdecl ___lc_collate_cp_func() {
  KdBreakPoint();
  return 0;
}
