#define _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING

#include <wdm.h>

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_UNLOAD DriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriverUnload)
#endif

int test_all();

#include <gtest/gtest.h>

#if CRTSYS_USE_NTL_MAIN
#include <iostream>
#include <ntl/driver>

#include <ntl/rpc/server>
// rpc server stub code
#include "common/rpc.hpp"

#include "common/test_device.h"

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

#ifndef CRTSYS_TEST_NO_BREAKPOINT
  KdBreakPoint();
#endif

  std::wcout << "load (registry_path :" << registry_path << ")\n";

  const int driver_test_failures = test_all();
  if (driver_test_failures != 0) {
    return status(STATUS_UNSUCCESSFUL);
  }

  struct test_extension {
    test_extension() : val(0), create_count(0), close_count(0) {
      std::cout << "constructor - val : " << val << '\n';
    }
    ~test_extension() { std::cout << "destroctor - val : " << val << '\n'; }

    void inc() { val++; }

    int val;
    int create_count;
    int close_count;
  };

  auto test_device_options = ntl::device_options()
                                 .name(TEST_DEVICE_NAME)
                                 .type(FILE_DEVICE_UNKNOWN)
                                 .exclusive();
  auto test_dev_result =
      driver.try_create_device<test_extension>(test_device_options);
  if (!test_dev_result) {
    return test_dev_result.status();
  }

  auto test_dev = std::move(test_dev_result).value();
  if (test_dev) {
    test_dev->extension().val = 100;
    test_dev->extension().inc();

    std::weak_ptr test_dev_weak = test_dev;
    test_dev->on_create([test_dev_weak](ntl::irp &irp) {
      if (auto test_dev = test_dev_weak.lock())
        test_dev->extension().create_count++;
      irp.succeed();
    });
    test_dev->on_close([test_dev_weak](ntl::irp &irp) {
      if (auto test_dev = test_dev_weak.lock())
        test_dev->extension().close_count++;
      irp.succeed();
    });
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
        constexpr char reply[] = "world";
        if (!out.write_bytes(reply, sizeof(reply))) {
          std::cout << "[FAILED] out_buffer is too small\n";
        }
      } else if (code == TEST_DEVICE_STATE_CTL) {
        test_device_state state{};
        if (auto test_dev = test_dev_weak.lock()) {
          state.create_count = test_dev->extension().create_count;
          state.close_count = test_dev->extension().close_count;
        }
        if (!out.write(state)) {
          std::cout << "[FAILED] state out_buffer is too small\n";
          out.clear();
        } else {
          auto *written = out.as<test_device_state>();
          if (written && written->create_count != state.create_count)
            std::cout << "[FAILED] state write verification failed\n";
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

#ifndef CRTSYS_TEST_NO_BREAKPOINT
  KdBreakPoint();
#endif

  const int driver_test_failures = test_all();
  if (driver_test_failures != 0) {
    return STATUS_UNSUCCESSFUL;
  }

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
