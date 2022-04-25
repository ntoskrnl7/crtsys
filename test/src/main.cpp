#include <wdm.h>

#include <stdio.h>
#define printf(...)                                                            \
  (DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_UNLOAD DriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriverUnload)
#endif

void test_all();

#if CRTSYS_USE_NTL_MAIN
#include <ntl/driver>

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {
  KdBreakPoint();

  test_all();

  driver.on_unload([registry_path]() {
    DbgPrint("unload (registry_path : %ws)\n", registry_path.c_str());
  });

  return status::ok();
}

#else
// clang-format off
EXTERN_C
NTSTATUS
DriverEntry (
  _In_ PDRIVER_OBJECT DriverObject,
  _In_ PUNICODE_STRING RegistryPath
  )
{
  // clang-format on
  PAGED_CODE();
  UNREFERENCED_PARAMETER(RegistryPath);

  KdBreakPoint();

  test_all();

  DriverObject->DriverUnload = DriverUnload;
  return STATUS_SUCCESS;
}

// clang-format off
EXTERN_C
VOID
DriverUnload (
  _In_ PDRIVER_OBJECT DriverObject
  )
{
  // clang-format on
  PAGED_CODE();
  UNREFERENCED_PARAMETER(DriverObject);
}
#endif
