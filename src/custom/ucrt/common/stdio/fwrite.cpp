#include "../../../common/crt/crt_internal.h"

EXTERN_C size_t __cdecl fwrite(void const *const buffer, size_t const size,
                               size_t const count, FILE *const stream) {
  if (stream == stdout) {
    ANSI_STRING string;
    string.MaximumLength = string.Length = (USHORT)(size * count);
    string.Buffer = (PCH)buffer;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "%Z", &string);
    return count;
  } else if (stream == stderr) {
    ANSI_STRING string;
    string.MaximumLength = string.Length = (USHORT)(size * count);
    string.Buffer = (PCH)buffer;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "%Z", &string);
    return count;
  }
  // untested :-(
  KdBreakPoint();
  return 0;
}
