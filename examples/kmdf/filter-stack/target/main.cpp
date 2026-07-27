#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include "kmdf_filter_stack_ntl_ioctl.hpp"

namespace {

struct target_state {
  std::atomic<std::uint32_t> requests{0};
  std::atomic<std::uint32_t> prepare_count{0};
  std::atomic<std::uint32_t> d0_count{0};
};

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  device.context<target_state>().prepare_count.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto d0_entry =
    +[](ntl::kmdf::device device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  device.context<target_state>().d0_count.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG control_code) noexcept {
  if (control_code != kmdf_filter_stack_ntl_sample::query_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  try {
    const auto input =
        request.try_input_buffer<
            kmdf_filter_stack_ntl_sample::query_request>();
    if (!input) {
      request.complete(input.status());
      return;
    }

    const auto output =
        request.try_output_buffer<
            kmdf_filter_stack_ntl_sample::query_reply>();
    if (!output) {
      request.complete(output.status());
      return;
    }

    auto &state = queue.owner().context<target_state>();
    const std::uint32_t request_count =
        state.requests.fetch_add(1, std::memory_order_relaxed) + 1;
    // METHOD_BUFFERED can expose the same system buffer for input and output.
    // Preserve the input before zero-initializing the larger reply.
    const std::uint32_t input_value = input.value()->value;
    const std::string message =
        std::format("target handled {}", input_value);

    *output.value() = {};
    output.value()->value = input_value + 1;
    output.value()->layers =
        kmdf_filter_stack_ntl_sample::target_layer;
    output.value()->target_requests = request_count;
    output.value()->prepare_count =
        state.prepare_count.load(std::memory_order_relaxed);
    output.value()->d0_count =
        state.d0_count.load(std::memory_order_relaxed);
    output.value()->server_irql =
        static_cast<std::uint32_t>(ntl::current_irql());
    const std::size_t message_size =
        (std::min)(message.size(), sizeof(output.value()->message) - 1);
    std::copy_n(message.data(), message_size, output.value()->message);
    output.value()->message[message_size] = '\0';

    request.complete(
        STATUS_SUCCESS,
        sizeof(kmdf_filter_stack_ntl_sample::query_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
};

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  init.device_type(FILE_DEVICE_UNKNOWN)
      .io_type(WdfDeviceIoBuffered)
      .power_pageable();

  ntl::kmdf::pnp_power_callbacks pnp;
  pnp.on_prepare_hardware<prepare_hardware>()
      .on_d0_entry<d0_entry>();
  init.pnp_power(pnp);

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  const auto created =
      init.try_create<target_state>(&device_attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  ntl::status status = device.try_create_interface(
      kmdf_filter_stack_ntl_sample::device_interface_guid);
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchParallel);
  queue_config.on_device_control<device_control>();
  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  const auto queue =
      ntl::kmdf::io_queue::try_create(device, queue_config, &queue_attributes);
  if (!queue)
    return queue.status();
  return STATUS_SUCCESS;
};

} // namespace

ntl::status ntl::kmdf::main(driver_builder &builder,
                            const std::wstring &registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  kmdf::driver_config config;
  config.on_device_add<device_add>();

  kmdf::object_attributes attributes;
  attributes.execution_level(WdfExecutionLevelPassive);
  const auto driver = builder.try_create(config, &attributes);
  return driver ? ntl::status::ok() : driver.status();
}
