#include "../../../common/crt/crt_internal.h"

#pragma warning(disable : 4100)

extern "C" int __cdecl _setmbcp_nolock(int,
                                       struct __crt_multibyte_data *ptmbci) {
  KdBreakPoint();
  return 0;
}
