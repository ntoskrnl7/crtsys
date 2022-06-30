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

  struct test_extension {
    test_extension() : val(0) {
      std::cout << "constructor - val : " << val << '\n';
    }
    ~test_extension() { std::cout << "destroctor - val : " << val << '\n'; }
    int val;
  };

  auto test_dev =
      driver.create_device<test_extension>(ntl::device::options()
                                               .name(L"test_device")
                                               .type(FILE_DEVICE_UNKNOWN)
                                               .exclusive());

  test_dev->extension<test_extension>().val = 100;

  test_dev->on_device_control([](uint32_t ctl_code, const uint8_t *in_buffer,
                                 size_t in_buffer_length, uint8_t *out_buffer,
                                 size_t *out_buffer_length) {
#define TEST_DEVICE_CTL                                                        \
  CTL_CODE(FILE_DEVICE_NTL_RPC, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
    if (ctl_code == TEST_DEVICE_CTL) {
      in_buffer;
      in_buffer_length;
    }
    std::string actual(reinterpret_cast<const char *>(in_buffer),
                       in_buffer_length);
    if (actual != "hello")
      std::cout << "[FAILED] expect : hello, actual : " << actual << '\n';

    if (out_buffer) {
      strcpy_s(reinterpret_cast<char *>(out_buffer), *out_buffer_length,
               "world");
    } else {
      std::cout << "[FAILED] out_buffer == null\n";
    }
  });

  driver.on_unload(
      [registry_path, test_dev, rpc_svr = test_rpc::init(driver)]() {
        if (test_dev)
          std::wcout << L"delete device :" << test_dev->name() << L'\n';
        std::wcout << L"unload (registry_path :" << registry_path << L")\n";
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
