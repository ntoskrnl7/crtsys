#include "../../../common/crt/crt_internal.h"
#include <time.h>

#pragma warning(disable : 4100)

EXTERN_C char *__cdecl _Getdays() {
  KdBreakPoint();
  return nullptr;
}

EXTERN_C char *__cdecl _Getmonths() {
  KdBreakPoint();
  return nullptr;
}

EXTERN_C wchar_t *__cdecl _W_Getdays() {
  KdBreakPoint();
  return nullptr;
}

EXTERN_C wchar_t *__cdecl _W_Getmonths() {
  KdBreakPoint();
  return nullptr;
}

EXTERN_C void *__cdecl _W_Gettnames();

EXTERN_C void *__cdecl _Gettnames() { return _W_Gettnames(); }

EXTERN_C size_t __cdecl _Strftime(char *const string, size_t const max_size,
                                  char const *const format,
                                  tm const *const timeptr,
                                  void *const lc_time_arg) {
  KdBreakPoint();
  return 0;
}
