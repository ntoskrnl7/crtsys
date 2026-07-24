#define _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING

#include <wdm.h>

EXTERN_C DRIVER_INITIALIZE DriverEntry;
EXTERN_C DRIVER_UNLOAD DriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriverUnload)
#endif

int test_all();
void record_driver_entry_failure(const char *stage, long code);

#include <gtest/gtest.h>
#include <string>

namespace {

class driver_gtest_failure_listener final
    : public testing::EmptyTestEventListener {
public:
  void OnTestEnd(const testing::TestInfo &test_info) override {
    if (test_info.result()->Passed())
      return;

    std::string stage = "gtest ";
    stage += test_info.test_suite_name();
    stage += '.';
    stage += test_info.name();
    record_driver_entry_failure(stage.c_str(), 1);
  }
};

void install_driver_gtest_failure_listener() {
  testing::UnitTest::GetInstance()->listeners().Append(
      new driver_gtest_failure_listener());
}

} // namespace

#if CRTSYS_USE_NTL_MAIN
#include <iostream>
#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/ipc/all>

#include <ntl/rpc/server>
// rpc server stub code
#include "common/rpc.hpp"

#include "common/test_device.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace {

struct mapped_test_session {
  ntl::ipc::process_connection connection;
  std::shared_ptr<ntl::ipc::page_buffer> pages;
  ntl::ipc::mapped_buffer_view view;
  PFILE_OBJECT file_object = nullptr;
};

struct test_extension {
  test_extension() : val(0), create_count(0), close_count(0) {
    std::cout << "constructor - val : " << val << '\n';
  }

  ~test_extension() {
    close_mapping(nullptr, false);
    std::cout << "destroctor - val : " << val << '\n';
  }

  void inc() { val++; }

  ntl::status begin_mapping(ntl::irp &request,
                            ntl::device_control::out_buffer &out) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return STATUS_INVALID_DEVICE_STATE;
    if (!out.can_write(sizeof(test_mapping_begin_result)))
      return STATUS_BUFFER_TOO_SMALL;

    {
      std::lock_guard<std::mutex> guard(mapping_lock);
      if (mapping)
        return STATUS_DEVICE_BUSY;
    }

    auto connection =
        ntl::ipc::process_connection::try_capture_current_process(
            {.max_mappings = 1, .max_mapped_bytes = PAGE_SIZE});
    if (!connection)
      return connection.status();
    auto pages = ntl::ipc::page_buffer::try_create(PAGE_SIZE);
    if (!pages)
      return pages.status();

    auto *const bytes = static_cast<unsigned char *>((*pages)->data());
    for (unsigned long index = 0; index != test_mapping_pattern_bytes; ++index)
      bytes[index] =
          static_cast<unsigned char>(test_mapping_initial_seed + index);

    auto mapped = ntl::ipc::try_map_page_buffer(
        *pages, 0, PAGE_SIZE, *connection,
        ntl::ipc::map_access::read_write);
    if (!mapped)
      return mapped.status();
    const auto descriptor = mapped->descriptor();
    if (!descriptor.valid()) {
      (void)mapped->close();
      return STATUS_INVALID_DEVICE_STATE;
    }

    try {
      std::lock_guard<std::mutex> guard(mapping_lock);
      if (mapping) {
        (void)mapped->close();
        return STATUS_DEVICE_BUSY;
      }
      mapping.emplace(mapped_test_session{
          *connection, *pages, *mapped, request.stack_location()->FileObject});
    } catch (...) {
      (void)mapped->close();
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    const test_mapping_begin_result result{
        descriptor.mapping_id, descriptor.generation,
        descriptor.mapped_address, descriptor.length, descriptor.access,
        descriptor.reserved, test_mapping_pattern_bytes, 0};
    if (!out.write(result)) {
      close_mapping(request.stack_location()->FileObject, false);
      return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
  }

  ntl::status close_mapping_explicit(
      ntl::irp &request, const ntl::device_control::in_buffer &in,
      ntl::device_control::out_buffer &out) noexcept {
    const auto *const close_request = in.as<test_mapping_close_request>();
    if (!close_request || !out.can_write(sizeof(test_mapping_close_result)))
      return STATUS_BUFFER_TOO_SMALL;

    std::optional<mapped_test_session> session;
    {
      std::lock_guard<std::mutex> guard(mapping_lock);
      if (!mapping ||
          mapping->file_object != request.stack_location()->FileObject)
        return STATUS_NOT_FOUND;
      session.emplace(std::move(*mapping));
      mapping.reset();
    }

    const auto before = session->connection.mapping_count();
    unsigned long observed = 0;
    const auto *const bytes =
        static_cast<const unsigned char *>(session->pages->data());
    for (; observed != test_mapping_pattern_bytes; ++observed) {
      if (bytes[observed] != close_request->expected_value)
        break;
    }
    const auto generation_before = session->connection.generation();
    const auto closed = session->connection.close();
    const auto generation_after = session->connection.generation();
    const auto closed_again = session->connection.close();
    const auto generation_after_repeat = session->connection.generation();
    const auto after = session->connection.mapping_count();
    explicit_mapping_closes.fetch_add(1, std::memory_order_relaxed);

    const test_mapping_close_result result{
        generation_before, generation_after, observed,
        static_cast<unsigned long>(before), static_cast<unsigned long>(after),
        0};
    if (!out.write(result))
      return STATUS_BUFFER_TOO_SMALL;
    if (static_cast<NTSTATUS>(closed_again) !=
            static_cast<NTSTATUS>(closed) ||
        generation_after_repeat != generation_after)
      return STATUS_INVALID_DEVICE_STATE;
    return closed;
  }

  void close_mapping(PFILE_OBJECT file_object, bool cleanup) noexcept {
    std::optional<mapped_test_session> session;
    {
      std::lock_guard<std::mutex> guard(mapping_lock);
      if (!mapping ||
          (file_object != nullptr && mapping->file_object != file_object))
        return;
      session.emplace(std::move(*mapping));
      mapping.reset();
    }
    (void)session->connection.close();
    if (cleanup)
      cleanup_mapping_closes.fetch_add(1, std::memory_order_relaxed);
  }

  unsigned long mapping_count() const noexcept {
    std::lock_guard<std::mutex> guard(mapping_lock);
    return mapping ? static_cast<unsigned long>(
                         mapping->connection.mapping_count())
                   : 0;
  }

  int val;
  int create_count;
  int close_count;
  mutable std::mutex mapping_lock;
  std::optional<mapped_test_session> mapping;
  std::atomic<unsigned long> explicit_mapping_closes{0};
  std::atomic<unsigned long> cleanup_mapping_closes{0};
};

ntl::status copy_from_target(const ntl::ipc::mapped_buffer_view *view,
                             void *destination,
                             std::size_t bytes) noexcept {
  if (!view || !destination || bytes > view->size())
    return STATUS_INVALID_PARAMETER;
  return ntl::ipc::detail::guarded_copy(destination, view->target_address(),
                                        bytes);
}

ntl::status copy_to_target(const ntl::ipc::mapped_buffer_view *view,
                           const void *source,
                           std::size_t bytes) noexcept {
  if (!view || !source || bytes > view->size() ||
      view->access() != ntl::ipc::map_access::read_write)
    return STATUS_INVALID_PARAMETER;
  return ntl::ipc::detail::guarded_copy(view->target_address(), source, bytes);
}

ntl::status test_automatic_io_mapping(
    ntl::irp &request, const ntl::device_control::code &code,
    ntl::device_control::out_buffer &out) noexcept {
  auto connection =
      ntl::ipc::process_connection::try_capture_current_process(
          {.max_mappings = 3, .max_mapped_bytes = PAGE_SIZE * 3});
  if (!connection)
    return connection.status();

  auto *const stack = request.stack_location();
  auto mapped = ntl::ipc::try_map_io_buffers(
      stack->DeviceObject, request.get(), *connection);
  if (!mapped)
    return mapped.status();

  NTSTATUS operation_status = STATUS_SUCCESS;
  if (code == TEST_DEVICE_AUTO_BUFFERED_CTL) {
    test_auto_io_buffer input{};
    operation_status = copy_from_target(mapped->input(), &input, sizeof(input));
    if (NT_SUCCESS(operation_status) && input.magic != test_auto_io_magic)
      operation_status = STATUS_DATA_ERROR;
    if (NT_SUCCESS(operation_status)) {
      const test_auto_io_buffer result{test_auto_io_magic,
                                       input.value ^ test_auto_io_mask};
      operation_status =
          copy_to_target(mapped->output(), &result, sizeof(result));
      out.size = sizeof(result);
    }
  } else if (code == TEST_DEVICE_AUTO_BUFFERED_TAIL_CTL) {
    test_auto_io_header input{};
    test_auto_io_extended_result initial{};
    operation_status =
        copy_from_target(mapped->input(), &input, sizeof(input));
    if (NT_SUCCESS(operation_status))
      operation_status =
          copy_from_target(mapped->output(), &initial, sizeof(initial));
    if (NT_SUCCESS(operation_status) &&
        (input.magic != test_auto_io_magic || initial.tail_zero_0 != 0 ||
         initial.tail_zero_1 != 0))
      operation_status = STATUS_DATA_ERROR;
    if (NT_SUCCESS(operation_status)) {
      const test_auto_io_extended_result result{
          test_auto_io_magic, input.sequence ^ test_auto_io_mask, 0, 0};
      operation_status =
          copy_to_target(mapped->output(), &result, sizeof(result));
      out.size = sizeof(result);
    }
  } else if (code == TEST_DEVICE_AUTO_IN_DIRECT_CTL) {
    test_auto_io_header header{};
    test_auto_io_buffer payload{};
    operation_status =
        copy_from_target(mapped->control_input(), &header, sizeof(header));
    if (NT_SUCCESS(operation_status))
      operation_status =
          copy_from_target(mapped->input(), &payload, sizeof(payload));
    if (NT_SUCCESS(operation_status) &&
        (header.magic != test_auto_io_magic ||
         payload.magic != test_auto_io_magic ||
         header.sequence != payload.value))
      operation_status = STATUS_DATA_ERROR;
    out.clear();
  } else if (code == TEST_DEVICE_AUTO_OUT_DIRECT_CTL ||
             code == TEST_DEVICE_AUTO_NEITHER_CTL) {
    test_auto_io_header header{};
    const auto *const logical_input =
        code == TEST_DEVICE_AUTO_OUT_DIRECT_CTL ? mapped->control_input()
                                                : mapped->input();
    operation_status =
        copy_from_target(logical_input, &header, sizeof(header));
    if (NT_SUCCESS(operation_status) &&
        header.magic != test_auto_io_magic)
      operation_status = STATUS_DATA_ERROR;
    if (NT_SUCCESS(operation_status)) {
      const test_auto_io_result result{
          test_auto_io_magic, header.sequence ^ test_auto_io_mask};
      operation_status =
          copy_to_target(mapped->output(), &result, sizeof(result));
      out.size = sizeof(result);
    }
  } else {
    operation_status = STATUS_INVALID_DEVICE_REQUEST;
  }

  const auto mapping_close = mapped->close();
  const auto connection_close = connection->close();
  if (NT_SUCCESS(operation_status) && !mapping_close.is_ok())
    operation_status = static_cast<NTSTATUS>(mapping_close);
  if (NT_SUCCESS(operation_status) && !connection_close.is_ok())
    operation_status = static_cast<NTSTATUS>(connection_close);
  return operation_status;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &registry_path) {

#ifndef CRTSYS_TEST_NO_BREAKPOINT
  KdBreakPoint();
#endif

  std::wcout << "load (registry_path :" << registry_path << ")\n";

  const int driver_test_failures = test_all();
  if (driver_test_failures != 0) {
    return status(STATUS_UNSUCCESSFUL);
  }

  auto test_device_options = ntl::device_options()
                                 .name(TEST_DEVICE_NAME)
                                 .type(FILE_DEVICE_UNKNOWN)
                                 .exclusive();
  auto test_endpoint_result = ntl::try_create_device_endpoint<test_extension>(
      driver, test_device_options, L"\\DosDevices\\CrtSysTestDevice");
  if (!test_endpoint_result) {
    record_driver_entry_failure(
        "try_create_device_endpoint",
        static_cast<NTSTATUS>(test_endpoint_result.status()));
    return test_endpoint_result.status();
  }

  auto test_endpoint =
      std::make_shared<ntl::device_endpoint<test_extension>>(
          std::move(test_endpoint_result).value());
  auto test_dev = test_endpoint->device();
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
    test_dev->on_cleanup([test_dev_weak](ntl::irp &irp) {
      if (auto test_dev = test_dev_weak.lock())
        test_dev->extension().close_mapping(
            irp.stack_location()->FileObject, true);
      irp.succeed();
    });
    test_dev->on_pending_device_control(
        [test_dev_weak](ntl::irp &request,
                        const ntl::device_control::code &code,
                        const ntl::device_control::in_buffer &in,
                        ntl::device_control::out_buffer &out) {
      NTSTATUS operation_status = STATUS_SUCCESS;
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
          state.active_mappings = test_dev->extension().mapping_count();
          state.explicit_mapping_closes =
              test_dev->extension().explicit_mapping_closes.load(
                  std::memory_order_relaxed);
          state.cleanup_mapping_closes =
              test_dev->extension().cleanup_mapping_closes.load(
                  std::memory_order_relaxed);
        }
        if (!out.write(state)) {
          std::cout << "[FAILED] state out_buffer is too small\n";
          out.clear();
        } else {
          auto *written = out.as<test_device_state>();
          if (written && written->create_count != state.create_count)
            std::cout << "[FAILED] state write verification failed\n";
        }
      } else if (code == TEST_DEVICE_MAPPING_BEGIN_CTL) {
        if (auto test_dev = test_dev_weak.lock())
          operation_status = static_cast<NTSTATUS>(
              test_dev->extension().begin_mapping(request, out));
        else
          operation_status = STATUS_DELETE_PENDING;
      } else if (code == TEST_DEVICE_MAPPING_CLOSE_CTL) {
        if (auto test_dev = test_dev_weak.lock())
          operation_status = static_cast<NTSTATUS>(
              test_dev->extension().close_mapping_explicit(request, in, out));
        else
          operation_status = STATUS_DELETE_PENDING;
      } else if (code == TEST_DEVICE_AUTO_BUFFERED_CTL ||
                 code == TEST_DEVICE_AUTO_BUFFERED_TAIL_CTL ||
                 code == TEST_DEVICE_AUTO_IN_DIRECT_CTL ||
                 code == TEST_DEVICE_AUTO_OUT_DIRECT_CTL ||
                 code == TEST_DEVICE_AUTO_NEITHER_CTL) {
        operation_status =
            static_cast<NTSTATUS>(test_automatic_io_mapping(request, code, out));
      } else {
        operation_status = STATUS_INVALID_DEVICE_REQUEST;
      }
      if (!NT_SUCCESS(operation_status)) {
        out.clear();
        request.fail(operation_status);
      }
      return ntl::device_control::dispatch_result::complete;
    });
  }

  ntl::rpc::server_options rpc_options(L"test_rpc");
  rpc_options.asynchronous().max_pending_calls(32);
  auto rpc_server = test_rpc::init(driver, rpc_options);

  driver.on_unload([registry_path, test_endpoint, rpc_server]() mutable {
    auto test_dev = test_endpoint->device();
    if (test_dev)
      std::wcout << L"delete device :" << test_dev->name() << " - "
                 << test_dev->extension().val << L'\n';
    test_endpoint->reset();
    rpc_server.reset();
    std::wcout << L"unload driver (registry_path :" << registry_path << L")\n";
  });

  testing::InitGoogleTest();
  install_driver_gtest_failure_listener();
  const int gtest_failures = RUN_ALL_TESTS();
  if (gtest_failures != 0) {
    record_driver_entry_failure("RUN_ALL_TESTS", gtest_failures);
    return status(STATUS_UNSUCCESSFUL);
  }
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

#ifndef CRTSYS_TEST_NO_BREAKPOINT
  KdBreakPoint();
#endif

  const int driver_test_failures = test_all();
  if (driver_test_failures != 0) {
    return STATUS_UNSUCCESSFUL;
  }

  DriverObject->DriverUnload = DriverUnload;

  testing::InitGoogleTest();
  install_driver_gtest_failure_listener();
  const int gtest_failures = RUN_ALL_TESTS();
  if (gtest_failures != 0) {
    record_driver_entry_failure("RUN_ALL_TESTS", gtest_failures);
    return STATUS_UNSUCCESSFUL;
  }
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
