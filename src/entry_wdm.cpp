#include "runtime_internal.h"

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_INITIALIZE CrtSysWdmDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysWdmDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysWdmDriverEntry)
#pragma alloc_text(PAGE, CrtSysWdmDriverUnload)
#endif

namespace {
PDRIVER_UNLOAD downstream_unload;
}

EXTERN_C
NTSTATUS
CrtSysWdmDriverEntry(PDRIVER_OBJECT driver_object,
                     PUNICODE_STRING registry_path) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeRuntime(driver_object, registry_path);
  if (!NT_SUCCESS(status))
    return status;

  status = DriverEntry(driver_object, registry_path);
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeRuntime(driver_object);
    return status;
  }

  downstream_unload = driver_object->DriverUnload;
  if (downstream_unload)
    driver_object->DriverUnload = CrtSysWdmDriverUnload;

  return status;
}

EXTERN_C
VOID
CrtSysWdmDriverUnload(PDRIVER_OBJECT driver_object) {
  PAGED_CODE();

  if (downstream_unload)
    downstream_unload(driver_object);

  CrtSysUninitializeRuntime(driver_object);
}
