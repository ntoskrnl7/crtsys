#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include "kmdf_reference_ioctl.hpp"

namespace {

struct device_state {
  std::atomic<std::uint32_t> next_session{1};
  std::atomic<std::uint32_t> next_sequence{1};
  std::atomic<std::uint32_t> open_handles{0};
  std::atomic<std::uint32_t> prepare_count{0};
  std::atomic<std::uint32_t> d0_entry_count{0};
  std::atomic<std::uint32_t> completed_requests{0};
  std::atomic<std::uint32_t> canceled_requests{0};
  std::atomic<bool> hardware_prepared{false};
  std::atomic<bool> in_d0{false};
};

struct file_state {
  std::uint32_t session_id = 0;
  bool cleaned_up = false;
};

struct queue_state {
  ntl::kmdf::timer timer;
  WDFREQUEST current_request = WDF_NO_HANDLE;
  std::uint32_t value = 0;
  std::uint32_t session_id = 0;
  std::uint32_t sequence = 0;
};

queue_state &state_for(ntl::kmdf::io_queue queue) noexcept {
  return ntl::kmdf::object{
      reinterpret_cast<WDFOBJECT>(queue.native_handle())}
      .context<queue_state>();
}

std::uint32_t session_for(const ntl::kmdf::request &request) noexcept {
  const ntl::kmdf::file file = request.associated_file();
  if (!file)
    return 0;
  const auto *context = file.try_context<file_state>();
  return context ? context->session_id : 0;
}

void fill_reply(kmdf_reference::status_reply &reply,
                const device_state &state, std::uint32_t value,
                std::uint32_t session_id, std::uint32_t sequence,
                const char *message) noexcept {
  reply = {};
  reply.header =
      kmdf_reference::make_header(sizeof(kmdf_reference::status_reply));
  reply.value = value;
  reply.session_id = session_id;
  reply.sequence = sequence;
  reply.open_handles =
      state.open_handles.load(std::memory_order_relaxed);
  reply.prepare_count =
      state.prepare_count.load(std::memory_order_relaxed);
  reply.d0_entry_count =
      state.d0_entry_count.load(std::memory_order_relaxed);
  reply.completed_requests =
      state.completed_requests.load(std::memory_order_relaxed);
  reply.canceled_requests =
      state.canceled_requests.load(std::memory_order_relaxed);
  reply.server_irql = static_cast<std::uint32_t>(ntl::current_irql());
  if (state.hardware_prepared.load(std::memory_order_acquire))
    reply.flags |= kmdf_reference::hardware_prepared;
  if (state.in_d0.load(std::memory_order_acquire))
    reply.flags |= kmdf_reference::device_in_d0;

  const std::size_t message_length =
      (std::min)(std::char_traits<char>::length(message),
                 sizeof(reply.message) - 1);
  std::copy_n(message, message_length, reply.message);
  reply.message[message_length] = '\0';
}

void complete_operation(queue_state &queue,
                        ntl::kmdf::request request) noexcept {
  auto &device = request.associated_queue().owner().context<device_state>();
  const std::uint32_t completed =
      device.completed_requests.fetch_add(1, std::memory_order_relaxed) + 1;

  const auto output =
      request.try_output_buffer<kmdf_reference::status_reply>();
  if (!output) {
    request.complete(output.status());
    return;
  }

  try {
    const std::uint32_t result = queue.value * 3u + 1u;
    const std::string message =
        std::format("session {} operation {} completed as {}",
                    queue.session_id, queue.sequence, result);
    fill_reply(*output.value(), device, result, queue.session_id,
               queue.sequence, message.c_str());
    output.value()->completed_requests = completed;
    request.complete(STATUS_SUCCESS,
                     sizeof(kmdf_reference::status_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
}

constexpr auto cancel_request = +[](ntl::kmdf::request request) noexcept {
  auto &queue = state_for(request.associated_queue());
  auto &device =
      request.associated_queue().owner().context<device_state>();
  (void)queue.timer.stop(false);
  if (queue.current_request == request.native_handle())
    queue.current_request = WDF_NO_HANDLE;
  device.canceled_requests.fetch_add(1, std::memory_order_relaxed);
  request.complete(STATUS_CANCELLED);
};

constexpr auto timer_expired = +[](ntl::kmdf::timer timer) noexcept {
  auto &queue = timer.parent().context<queue_state>();
  if (queue.current_request == WDF_NO_HANDLE)
    return;

  ntl::kmdf::request request{queue.current_request};
  const ntl::status cancel_status = request.try_unmark_cancelable();
  if (cancel_status == STATUS_CANCELLED)
    return;
  if (cancel_status.is_err()) {
    queue.current_request = WDF_NO_HANDLE;
    request.complete(cancel_status);
    return;
  }

  queue.current_request = WDF_NO_HANDLE;
  complete_operation(queue, std::move(request));
};

constexpr auto file_create =
    +[](ntl::kmdf::device device, ntl::kmdf::request request,
        ntl::kmdf::file file) noexcept {
  auto &device_context = device.context<device_state>();
  auto &file_context = file.context<file_state>();
  file_context.session_id =
      device_context.next_session.fetch_add(1, std::memory_order_relaxed);
  device_context.open_handles.fetch_add(1, std::memory_order_relaxed);
  request.complete(STATUS_SUCCESS);
};

constexpr auto file_cleanup = +[](ntl::kmdf::file file) noexcept {
  file.context<file_state>().cleaned_up = true;
};

constexpr auto file_close = +[](ntl::kmdf::file file) noexcept {
  file.owner().context<device_state>().open_handles.fetch_sub(
      1, std::memory_order_relaxed);
};

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  state.prepare_count.fetch_add(1, std::memory_order_relaxed);
  state.hardware_prepared.store(true, std::memory_order_release);
  return STATUS_SUCCESS;
};

constexpr auto release_hardware =
    +[](ntl::kmdf::device device,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  device.context<device_state>().hardware_prepared.store(
      false, std::memory_order_release);
  return STATUS_SUCCESS;
};

constexpr auto d0_entry =
    +[](ntl::kmdf::device device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  state.d0_entry_count.fetch_add(1, std::memory_order_relaxed);
  state.in_d0.store(true, std::memory_order_release);
  return STATUS_SUCCESS;
};

constexpr auto d0_exit =
    +[](ntl::kmdf::device device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  device.context<device_state>().in_d0.store(false,
                                             std::memory_order_release);
  return STATUS_SUCCESS;
};

void complete_query(ntl::kmdf::io_queue queue,
                    ntl::kmdf::request request) noexcept {
  const auto input =
      request.try_input_buffer<kmdf_reference::query_request>();
  if (!input) {
    request.complete(input.status());
    return;
  }

  // METHOD_BUFFERED aliases the kernel input and output system buffer. Read
  // and validate the complete input contract before clearing the reply.
  const kmdf_reference::abi_header header = input.value()->header;
  if (!kmdf_reference::valid_header(
          header, sizeof(kmdf_reference::query_request))) {
    request.complete(STATUS_REVISION_MISMATCH);
    return;
  }

  const auto output =
      request.try_output_buffer<kmdf_reference::status_reply>();
  if (!output) {
    request.complete(output.status());
    return;
  }

  auto &state = queue.owner().context<device_state>();
  if (!state.hardware_prepared.load(std::memory_order_acquire) ||
      !state.in_d0.load(std::memory_order_acquire)) {
    request.complete(STATUS_INVALID_DEVICE_STATE);
    return;
  }

  fill_reply(*output.value(), state, 0, session_for(request), 0, "ready");
  request.complete(STATUS_SUCCESS, sizeof(kmdf_reference::status_reply));
}

void begin_operation(ntl::kmdf::io_queue queue,
                     ntl::kmdf::request request) noexcept {
  const auto input =
      request.try_input_buffer<kmdf_reference::operation_request>();
  if (!input) {
    request.complete(input.status());
    return;
  }

  const kmdf_reference::operation_request operation = *input.value();
  if (!kmdf_reference::valid_header(
          operation.header,
          sizeof(kmdf_reference::operation_request))) {
    request.complete(STATUS_REVISION_MISMATCH);
    return;
  }
  if (operation.delay_ms == 0 ||
      operation.delay_ms > kmdf_reference::maximum_delay_ms) {
    request.complete(STATUS_INVALID_PARAMETER);
    return;
  }

  auto &device = queue.owner().context<device_state>();
  if (!device.hardware_prepared.load(std::memory_order_acquire) ||
      !device.in_d0.load(std::memory_order_acquire)) {
    request.complete(STATUS_INVALID_DEVICE_STATE);
    return;
  }

  auto &state = state_for(queue);
  if (state.current_request != WDF_NO_HANDLE) {
    request.complete(STATUS_DEVICE_BUSY);
    return;
  }

  state.current_request = request.native_handle();
  state.value = operation.value;
  state.session_id = session_for(request);
  state.sequence =
      device.next_sequence.fetch_add(1, std::memory_order_relaxed);

  const ntl::status status =
      request.try_mark_cancelable<cancel_request>();
  if (status.is_err()) {
    state.current_request = WDF_NO_HANDLE;
    if (status == STATUS_CANCELLED) {
      device.canceled_requests.fetch_add(1, std::memory_order_relaxed);
    }
    request.complete(status);
    return;
  }

  (void)state.timer.start_after_ms(operation.delay_ms);
}

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG control_code) noexcept {
  if (control_code == kmdf_reference::query_ioctl) {
    complete_query(queue, std::move(request));
    return;
  }
  if (control_code == kmdf_reference::operation_ioctl) {
    begin_operation(queue, std::move(request));
    return;
  }
  request.complete(STATUS_INVALID_DEVICE_REQUEST);
};

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  init.device_type(FILE_DEVICE_UNKNOWN)
      .io_type(WdfDeviceIoBuffered)
      .power_pageable();

  ntl::kmdf::pnp_power_callbacks pnp;
  pnp.on_prepare_hardware<prepare_hardware>()
      .on_release_hardware<release_hardware>()
      .on_d0_entry<d0_entry>()
      .on_d0_exit<d0_exit>();
  init.pnp_power(pnp);

  ntl::kmdf::file_config<file_state> files;
  files.execution_level(WdfExecutionLevelPassive)
      .on_create<file_create>()
      .on_cleanup<file_cleanup>()
      .on_close<file_close>();
  init.file_objects(files);

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  const auto created =
      init.try_create<device_state>(&device_attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  ntl::status status =
      device.try_create_interface(kmdf_reference::device_interface_guid);
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

  const auto queue_context =
      ntl::kmdf::object{
          reinterpret_cast<WDFOBJECT>(queue->native_handle())}
          .try_emplace_context<queue_state>();
  if (!queue_context)
    return queue_context.status();

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
  queue_context.value()->timer = timer.value();
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
