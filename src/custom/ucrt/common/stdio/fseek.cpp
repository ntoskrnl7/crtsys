#include "../../../common/crt/crt_internal.h"

#pragma warning(disable : 4100)

EXTERN_C int __cdecl fseek(
    FILE* const public_stream,
    long  const offset,
    int   const whence
    )
{
    // untested :-(
    KdBreakPoint();
    return 0;
}

EXTERN_C int __cdecl _fseeki64(FILE *const public_stream, __int64 const offset,
                               int const whence) {
  // untested :-(
  KdBreakPoint();
  return 0;
}
