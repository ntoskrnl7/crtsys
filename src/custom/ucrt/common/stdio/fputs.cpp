#include "../../../common/crt/crt_internal.h"

EXTERN_C int __cdecl fputs(char const *const string, FILE *const stream) {
  if (stream == stdout) {
    ANSI_STRING ansi;
    ansi.MaximumLength = ansi.Length = (USHORT)strlen(string);
    ansi.Buffer = (PCH)string;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "%Z", &ansi);
    return 0;
  } else if (stream == stderr) {
    ANSI_STRING ansi;
    ansi.MaximumLength = ansi.Length = (USHORT)strlen(string);
    ansi.Buffer = (PCH)string;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "%Z", &ansi);
    return 0;
  }
  // untested :-(
  KdBreakPoint();
  return 0;
}