#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

#include "kmdf_echo_ntl_ioctl.hpp"

namespace {

struct queue_state {
  ntl::kmdf::timer timer;
  WDFREQUEST current_request = WDF_NO_HANDLE;
  std::uint32_t completed_requests = 0;
  std::uint32_t canceled_requests = 0;
};

queue_state &state_for(ntl::kmdf::io_queue queue) noexcept {
  return ntl::kmdf::object{
      reinterpret_cast<WDFOBJECT>(queue.native_handle())}
      .context<queue_state>();
}

constexpr auto cancel_request = +[](ntl::kmdf::request request) noexcept {
  auto &state = state_for(request.associated_queue());
  (void)state.timer.stop(false);
  if (state.current_request == request.native_handle())
    state.current_request = WDF_NO_HANDLE;
  ++state.canceled_requests;
  request.complete(STATUS_CANCELLED);
};

void complete_echo(queue_state &state,
                   ntl::kmdf::request request) noexcept {
  try {
    const auto input =
        request.try_input_buffer<kmdf_echo_ntl_sample::echo_request>();
    if (!input) {
      request.complete(input.status());
      return;
    }

    const auto output =
        request.try_output_buffer<kmdf_echo_ntl_sample::echo_reply>();
    if (!output) {
      request.complete(output.status());
      return;
    }

    ++state.completed_requests;
    // METHOD_BUFFERED can expose the same system buffer for input and output.
    // Snapshot the request before clearing the larger reply.
    const std::uint32_t input_value = input.value()->value;
    const std::uint32_t delay_ms = input.value()->delay_ms;
    const std::string message =
        std::format("KMDF echoed {} after {} ms", input_value, delay_ms);

    *output.value() = {};
    output.value()->value = input_value;
    output.value()->delay_ms = delay_ms;
    output.value()->completed_requests = state.completed_requests;
    output.value()->canceled_requests = state.canceled_requests;
    output.value()->server_irql =
        static_cast<std::uint32_t>(ntl::current_irql());
    const std::size_t message_size =
        (std::min)(message.size(), sizeof(output.value()->message) - 1);
    std::copy_n(message.data(), message_size, output.value()->message);
    output.value()->message[message_size] = '\0';

    request.complete(STATUS_SUCCESS,
                     sizeof(kmdf_echo_ntl_sample::echo_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
}

constexpr auto timer_expired = +[](ntl::kmdf::timer timer) noexcept {
  auto &state = timer.parent().context<queue_state>();
  if (state.current_request == WDF_NO_HANDLE)
    return;

  ntl::kmdf::request request{state.current_request};
  const ntl::status cancel_status = request.try_unmark_cancelable();
  if (cancel_status == STATUS_CANCELLED)
    return;
  if (cancel_status.is_err()) {
    state.current_request = WDF_NO_HANDLE;
    request.complete(cancel_status);
    return;
  }

  state.current_request = WDF_NO_HANDLE;
  complete_echo(state, std::move(request));
};

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG control_code) noexcept {
  if (control_code != kmdf_echo_ntl_sample::echo_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  const auto input =
      request.try_input_buffer<kmdf_echo_ntl_sample::echo_request>();
  if (!input) {
    request.complete(input.status());
    return;
  }
  if (input.value()->delay_ms == 0 ||
      input.value()->delay_ms > 30'000) {
    request.complete(STATUS_INVALID_PARAMETER);
    return;
  }

  auto &state = state_for(queue);
  if (state.current_request != WDF_NO_HANDLE) {
    request.complete(STATUS_DEVICE_BUSY);
    return;
  }

  state.current_request = request.native_handle();
  const ntl::status status =
      request.try_mark_cancelable<cancel_request>();
  if (status.is_err()) {
    state.current_request = WDF_NO_HANDLE;
    if (status == STATUS_CANCELLED)
      ++state.canceled_requests;
    request.complete(status);
    return;
  }

  (void)state.timer.start_after_ms(input.value()->delay_ms);
};

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  init.device_type(FILE_DEVICE_UNKNOWN)
      .io_type(WdfDeviceIoBuffered)
      .power_pageable();

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  const auto created = init.try_create(&device_attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  ntl::status status =
      device.try_create_interface(kmdf_echo_ntl_sample::device_interface_guid);
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_device_control<device_control>();

  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive)
      .synchronization_scope(WdfSynchronizationScopeQueue);
  const auto queue =
      ntl::kmdf::io_queue::try_create(device, queue_config, &queue_attributes);
  if (!queue)
    return queue.status();

  auto context =
      ntl::kmdf::object{
          reinterpret_cast<WDFOBJECT>(queue->native_handle())}
          .try_emplace_context<queue_state>();
  if (!context)
    return context.status();

  auto timer_config =
      ntl::kmdf::timer_config::one_shot<timer_expired>();
  timer_config.automatic_serialization(true);
  ntl::kmdf::object_attributes timer_attributes;
  timer_attributes.execution_level(WdfExecutionLevelPassive);
  const auto timer =
      ntl::kmdf::timer::try_create(queue.value(), timer_config,
                                   &timer_attributes);
  if (!timer)
    return timer.status();
  context.value()->timer = timer.value();

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
