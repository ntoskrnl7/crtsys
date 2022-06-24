#include <wdm.h>

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_UNLOAD DriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriverUnload)
#endif

void test_all();

#include <gtest/gtest.h>

#if CRTSYS_USE_NTL_MAIN
#include <iostream>
#include <ntl/driver>

#include <ntl/rpc/server>
// rpc server stub code
#include "common/rpc.hpp"

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

  KdBreakPoint();

  std::wcout << "load (registry_path :" << registry_path << ")\n";

  test_all();

  driver.on_unload([registry_path, rpc_svr = test_rpc::init(driver)]() {
    std::wcout << "unload (registry_path :" << registry_path << ")\n";
  });

  testing::InitGoogleTest();
  return RUN_ALL_TESTS() == 0 ? status::ok() : status(STATUS_UNSUCCESSFUL);
}
#else  // !CRTSYS_USE_NTL_MAIN
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

  testing::InitGoogleTest();
  return RUN_ALL_TESTS() == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
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
#endif // !CRTSYS_USE_NTL_MAIN
