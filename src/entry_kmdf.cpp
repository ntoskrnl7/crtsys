#include "runtime_internal.h"

EXTERN_C DRIVER_INITIALIZE FxDriverEntry;
EXTERN_C DRIVER_INITIALIZE CrtSysKmdfDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysKmdfDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysKmdfDriverEntry)
#pragma alloc_text(PAGE, CrtSysKmdfDriverUnload)
#endif

namespace {
PDRIVER_UNLOAD wdf_driver_unload;
}

EXTERN_C
NTSTATUS
CrtSysKmdfDriverEntry(PDRIVER_OBJECT driver_object,
                      PUNICODE_STRING registry_path) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeRuntime(driver_object, registry_path);
  if (!NT_SUCCESS(status))
    return status;

  status = FxDriverEntry(driver_object, registry_path);
  if (!NT_SUCCESS(status)) {
    CrtSysUninitializeRuntime(driver_object);
    return status;
  }

  wdf_driver_unload = driver_object->DriverUnload;
  if (wdf_driver_unload)
    driver_object->DriverUnload = CrtSysKmdfDriverUnload;

  return status;
}

EXTERN_C
VOID
CrtSysKmdfDriverUnload(PDRIVER_OBJECT driver_object) {
  PAGED_CODE();

  if (wdf_driver_unload)
    wdf_driver_unload(driver_object);

  CrtSysUninitializeRuntime(driver_object);
}
