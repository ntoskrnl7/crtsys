#include "runtime_internal.h"

#include "../include/ntl/driver"
#include "../include/ntl/expand_stack"
#include "ntl_device_dispatch.h"

#include <memory>

EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysDriverEntry)
#pragma alloc_text(PAGE, CrtSysDriverUnload)
#endif

namespace {
std::unique_ptr<ntl::driver> this_driver;
}

namespace ntl {
class driver_initializer {
public:
  static std::unique_ptr<ntl::driver> make_driver(PDRIVER_OBJECT object) {
    return std::make_unique<ntl::driver>(std::move(ntl::driver(object)));
  }
};

class driver_unload_invoker {
public:
  static void unload(driver &driver) {
    if (driver.unload_routine_)
      driver.unload_routine_();
  }
};

} // namespace ntl

static NTSTATUS CrtSysNtlDispatchRoutine(PDEVICE_OBJECT device_object,
                                         PIRP irp) {
  if (this_driver)
    return ntl::device_dispatch_invoker::invoke(*this_driver, device_object,
                                                irp);

  irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
  irp->IoStatus.Information = 0;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return STATUS_INVALID_DEVICE_REQUEST;
}

EXTERN_C
NTSTATUS
CrtSysDriverEntry(PDRIVER_OBJECT driver_object,
                  PUNICODE_STRING registry_path) {
  PAGED_CODE();

  NTSTATUS status = CrtSysInitializeRuntime(driver_object, registry_path);
  if (!NT_SUCCESS(status))
    return status;

  try {
    auto driver = ntl::driver_initializer::make_driver(driver_object);
    ntl::status entry_status = ntl::expand_stack(
        ntl::main, std::ref(*driver), std::wstring(registry_path->Buffer));
    if (!entry_status.is_ok()) {
      ntl::driver_unload_invoker::unload(*driver);
      driver.reset();
      CrtSysUninitializeRuntime(driver_object);
      return entry_status;
    }
    this_driver = std::move(driver);
  } catch (const ntl::exception &e) {
    CrtSysUninitializeRuntime(driver_object);
    return e.get_status();
  } catch (...) {
    CrtSysUninitializeRuntime(driver_object);
    return STATUS_UNSUCCESSFUL;
  }

  for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i)
    driver_object->MajorFunction[i] = CrtSysNtlDispatchRoutine;
  driver_object->DriverUnload = CrtSysDriverUnload;
  return STATUS_SUCCESS;
}

EXTERN_C
VOID
CrtSysDriverUnload(PDRIVER_OBJECT driver_object) {
  PAGED_CODE();

  if (this_driver)
    ntl::driver_unload_invoker::unload(*this_driver);

  this_driver.reset();
  CrtSysUninitializeRuntime(driver_object);
}
