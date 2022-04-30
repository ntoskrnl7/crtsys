#include "setlocal.h"

#pragma warning(disable : 4100)

//
// getqloc.c
//

BOOL __cdecl __get_qualified_locale(const LPLC_STRINGS in_str, LPLC_ID out_id,
                                    LPLC_STRINGS out_str) {
  // :-( untested
  KdBreakPoint();
  return FALSE;
}

//
// initcoll.c
//
int __cdecl __init_collate(void) {
  KdBreakPoint();
  return 0;
}

//
// initctyp.c
//
int __cdecl __init_ctype(void) {
  KdBreakPoint();
  return 0;
}

//
// initmon.c
//
int __cdecl __init_monetary(void) {
  // :-( untested
  KdBreakPoint();
  return 0;
}
void __cdecl __free_lconv_mon(struct lconv *l) {
  // :-( untested
  KdBreakPoint();
}

//
// initnum.c
//
int __cdecl __init_numeric(void) {
  KdBreakPoint();
  return 0;
}
void __cdecl __free_lconv_num(struct lconv *l) {
  // :-( untested
  KdBreakPoint();
}

//
// inittime.c
//
int __cdecl __init_time(void) {
  KdBreakPoint();
  return 0;
}
void __cdecl __free_lc_time(struct __lc_time_data *lc_time) {
  // :-( untested
  KdBreakPoint();
}