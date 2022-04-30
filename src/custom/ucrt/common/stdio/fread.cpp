#include "../../../common/crt/crt_internal.h"

#pragma warning(disable : 4100)

EXTERN_C size_t __cdecl fread(void *const buffer, size_t const element_size,
                              size_t const element_count, FILE *const stream) {
  // untested :-(
  KdBreakPoint();
  return 0;
}
