#include "../../../common/crt/crt_internal.h"

EXTERN_C
wint_t __cdecl fputwc(wchar_t const c, FILE *const stream) {
  if (stream == stdout) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "%wc", c);
    return c;
  } else if (stream == stderr) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "%wc", c);
    return c;
  }
  // untested :-(
  KdBreakPoint();
  return 0;
}