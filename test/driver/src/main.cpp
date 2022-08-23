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

#include "common/test_device.h"

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

  KdBreakPoint();

  std::wcout << "load (registry_path :" << registry_path << ")\n";

  test_all();

  struct test_extension {
    test_extension() : val(0) {
      std::cout << "constructor - val : " << val << '\n';
    }
    ~test_extension() { std::cout << "destroctor - val : " << val << '\n'; }

    void inc() { val++; }

    int val;
  };

  auto test_dev =
      driver.create_device<test_extension>(ntl::device_options()
                                               .name(TEST_DEVICE_NAME)
                                               .type(FILE_DEVICE_UNKNOWN)
                                               .exclusive());
  if (test_dev) {
    test_dev->extension().val = 100;
    test_dev->extension().inc();

    std::weak_ptr test_dev_weak = test_dev;
    test_dev->on_device_control([test_dev_weak](
                                    const ntl::device_control::code &code,
                                    const ntl::device_control::in_buffer &in,
                                    ntl::device_control::out_buffer &out) {
      if (code == TEST_DEVICE_CTL) {
        if (auto test_dev = test_dev_weak.lock())
          test_dev->extension().val--;
        std::string actual(reinterpret_cast<const char *>(in.ptr), in.size);
        if (actual != "hello")
          std::cout << "[FAILED] expect : hello, actual : " << actual << '\n';
        if (out.ptr) {
          strcpy_s(reinterpret_cast<char *>(out.ptr), out.size, "world");
        } else {
          std::cout << "[FAILED] out_buffer == null\n";
        }
      }
    });
  }

  driver.on_unload([registry_path, test_dev,
                    rpc_svr = test_rpc::init(driver)]() mutable {
    if (test_dev)
      std::wcout << L"delete device :" << test_dev->name() << " - "
                 << test_dev->extension().val << L'\n';
    std::wcout << L"unload driver (registry_path :" << registry_path << L")\n";
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
