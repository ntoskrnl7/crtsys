#include "../../../common/crt/crt_internal.h"

#pragma warning(disable : 4100)

EXTERN_C int __cdecl setvbuf(FILE *const public_stream, char *const buffer,
                             int const type,
                             size_t const buffer_size_in_bytes) {
  // untested :-(
  KdBreakPoint();
  return 0;
}
