#include "../../crtsys.h"

#include <ntddk.h>

namespace {
#if DBG
volatile LONG g_filesystem_irql_warning_reported = 0;
#endif

#if defined(CRTSYS_ENABLE_RUNTIME_DIAGNOSTIC_TEST_HOOKS)
volatile LONG g_filesystem_irql_check_count = 0;
#endif
} // namespace

extern "C" void __cdecl CrtSysValidateFilesystemIrql() noexcept {
#if defined(CRTSYS_ENABLE_RUNTIME_DIAGNOSTIC_TEST_HOOKS)
  InterlockedIncrement(&g_filesystem_irql_check_count);
#endif

#if DBG
  const KIRQL current_irql = KeGetCurrentIrql();
  if (current_irql != PASSIVE_LEVEL &&
      InterlockedCompareExchange(&g_filesystem_irql_warning_reported, 1, 0) ==
          0) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
               "[crtsys] std::filesystem OS operation requires "
               "PASSIVE_LEVEL (current IRQL=%lu).\n",
               static_cast<unsigned long>(current_irql));
  }
#endif
}

#if defined(CRTSYS_ENABLE_RUNTIME_DIAGNOSTIC_TEST_HOOKS)
extern "C" LONG __cdecl
__crtsys_runtime_test_filesystem_irql_check_count() noexcept {
  return InterlockedCompareExchange(&g_filesystem_irql_check_count, 0, 0);
}
#endif
