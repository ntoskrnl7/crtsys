#if (!CRTSYS_USE_LIBCNTPR) || CRTSYS_NEED_CRT_ABORT

#include <vcstartup_internal.h>

extern "C" void __cdecl abort() {
  // untested :-(
  KdBreakPoint();
}
#endif