#include "runtime_internal.h"

#include "../include/ntl/expand_stack"
#include "../include/ntl/kmdf/driver"

#include <functional>
#include <string>

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_INITIALIZE CrtSysNtlKmdfDriverEntry;
EXTERN_C DRIVER_INITIALIZE FxDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysNtlKmdfDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysNtlKmdfDriverEntry)
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, CrtSysNtlKmdfDriverUnload)
#endif

namespace {
PDRIVER_UNLOAD wdf_driver_unload;

std::wstring copy_registry_path(PCUNICODE_STRING registry_path) {
  if (!registry_path || !registry_path->Buffer)
    return {};

  return std::wstring(registry_path->Buffer,
                      registry_path->Length / sizeof(WCHAR));
}
} // namespace

EXTERN_C
NTSTATUS
DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
  PAGED_CODE();

  try {
    ntl::kmdf::driver_builder builder(driver_object, registry_path);
    const ntl::status result =
        ntl::expand_stack(ntl::kmdf::main, std::ref(builder),
                          copy_registry_path(registry_path));
    return static_cast<NTSTATUS>(result);
  } catch (const ntl::exception &e) {
    return e.get_status();
  } catch (...) {
    return STATUS_UNSUCCESSFUL;
  }
}

EXTERN_C
NTSTATUS
CrtSysNtlKmdfDriverEntry(PDRIVER_OBJECT driver_object,
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
    driver_object->DriverUnload = CrtSysNtlKmdfDriverUnload;

  return status;
}

EXTERN_C
VOID
CrtSysNtlKmdfDriverUnload(PDRIVER_OBJECT driver_object) {
  PAGED_CODE();

  if (wdf_driver_unload)
    wdf_driver_unload(driver_object);

  CrtSysUninitializeRuntime(driver_object);
}
