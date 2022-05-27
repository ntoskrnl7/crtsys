#include <ntdef.h>
typedef PSTRING PUTF8_STRING;

#ifndef DECLSPEC_RESTRICT
#if (_MSC_VER >= 1915) && !defined(MIDL_PASS)
#define DECLSPEC_RESTRICT   __declspec(restrict)
#else
#define DECLSPEC_RESTRICT
#endif
#endif

#ifndef NTDDI_WIN10_FE
#define NTDDI_WIN10_FE                      0x0A00000A
#endif
#ifndef NTDDI_WIN10_FE
#define NTDDI_WIN10_CO                      0x0A00000B
#endif
#ifndef ENCLAVE_SHORT_ID_LENGTH
#define ENCLAVE_SHORT_ID_LENGTH             16
#endif
#ifndef ENCLAVE_LONG_ID_LENGTH
#define ENCLAVE_LONG_ID_LENGTH              32
#endif

#include <stdio.h>
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

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

  KdBreakPoint();

  std::wcout << "load (registry_path :" << registry_path << ")\n";

  testing::InitGoogleTest();
  RUN_ALL_TESTS();

  test_all();

  driver.on_unload([registry_path]() {
    std::wcout << "unload (registry_path :" << registry_path << ")\n";
  });

  return status::ok();
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

  testing::InitGoogleTest();
  RUN_ALL_TESTS();

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
#endif // !CRTSYS_USE_NTL_MAIN
