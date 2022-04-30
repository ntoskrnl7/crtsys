#include "../../../common/crt/crt_internal.h"

#include <time.h>

#pragma warning(disable : 4100)

EXTERN_C void *__cdecl _W_Gettnames() {
  KdBreakPoint();
  return nullptr;
}

EXTERN_C _Success_(return > 0) size_t
    __cdecl _Wcsftime(_Out_writes_z_(max_size) wchar_t *const buffer,
                      _In_ size_t const max_size,
                      _In_z_ wchar_t const *const format,
                      _In_ tm const *const timeptr,
                      _In_opt_ void *const lc_time_arg) {
  KdBreakPoint();
  return 0;
}
