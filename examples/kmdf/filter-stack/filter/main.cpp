#include <ntddk.h>

#include <ntl/kmdf/all>

#include <atomic>
#include <cstdint>

#include "kmdf_filter_stack_ntl_ioctl.hpp"

namespace {

struct filter_state {
  std::atomic<std::uint32_t> completions{0};
};

constexpr auto forward_completion =
    +[](ntl::kmdf::request request, ntl::kmdf::io_target,
        ntl::kmdf::completion_params params, void *context) noexcept {
  auto &state = *static_cast<filter_state *>(context);
  ULONG_PTR information = params.information();
  ntl::status status = params.status();

  if (status.is_ok() &&
      information >= sizeof(kmdf_filter_stack_ntl_sample::query_reply)) {
    const auto output =
        request.try_output_buffer<
            kmdf_filter_stack_ntl_sample::query_reply>();
    if (!output) {
      status = output.status();
      information = 0;
    } else {
      output.value()->value += 10;
      output.value()->layers |=
          kmdf_filter_stack_ntl_sample::filter_layer;
      output.value()->filter_completions =
          state.completions.fetch_add(1, std::memory_order_relaxed) + 1;
    }
  }

  request.complete(status, information);
};

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG) noexcept {
  const ntl::kmdf::device device = queue.owner();
  const ntl::kmdf::io_target target = device.default_io_target();
  auto &state = device.context<filter_state>();

  request.format_current_type();
  request.on_completion<forward_completion>(&state);
  const ntl::status status =
      std::move(request).try_send(target);
  if (status.is_err())
    request.complete(status);
};

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  init.filter();

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  const auto created =
      init.try_create<filter_state>(&device_attributes);
  if (!created)
    return created.status();

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchParallel);
  queue_config.on_device_control<device_control>();
  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  const auto queue = ntl::kmdf::io_queue::try_create(
      created.value(), queue_config, &queue_attributes);
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
