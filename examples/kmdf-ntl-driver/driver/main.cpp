#include <ntddk.h>
#include <wdmsec.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "kmdf_ntl_ioctl.hpp"

namespace {

struct device_state {
  std::vector<std::uint32_t> transformed_values;
  std::atomic<std::uint32_t> open_files{0};
  std::atomic<std::uint32_t> work_items{0};
  std::atomic<std::uint32_t> timer_expirations{0};

  ~device_state() noexcept {
    DbgPrint("[crtsys KMDF sample] device context destroyed\n");
  }
};

struct file_state {
  std::wstring name;
  bool cleaned_up = false;

  ~file_state() noexcept {
    DbgPrint("[crtsys KMDF sample] file context destroyed\n");
  }
};

struct device_identity {
  explicit device_identity(std::uint32_t value) noexcept : value(value) {}
  std::uint32_t value;
};

struct child_identity {
  ULONG serial_number;
};

struct child_location {
  ULONG slot;
};

constexpr auto child_create_callback =
    +[](ntl::kmdf::child_list,
        const ntl::kmdf::child_identification<child_identity> &,
        ntl::kmdf::device_init) noexcept -> NTSTATUS {
  return STATUS_NOT_SUPPORTED;
};

constexpr auto child_scan_callback =
    +[](ntl::kmdf::child_list) noexcept {};

constexpr auto child_compare_callback =
    +[](ntl::kmdf::child_list,
        const ntl::kmdf::child_identification<child_identity> &left,
        const ntl::kmdf::child_identification<child_identity>
            &right) noexcept {
  return left.payload.serial_number == right.payload.serial_number;
};

[[maybe_unused]] void compile_child_list_surface(ntl::kmdf::device device) {
  using child_config =
      ntl::kmdf::child_list_config<child_identity, child_location>;
  auto config = child_config::with_create<child_create_callback>();
  config.on_scan<child_scan_callback>()
      .on_compare<child_compare_callback>();

  auto list = ntl::kmdf::child_list::try_create(device, config);
  if (!list)
    return;

  const ntl::kmdf::child_identification<child_identity> child{{42}};
  const ntl::kmdf::child_address<child_location> address{{3}};
  (void)list->add_or_update(child, address);
  (void)list->find(child);
  auto iteration = list->iterate();
  (void)iteration.next();
}

void complete_transform(device_state &state,
                        ntl::kmdf::request request) noexcept;

constexpr auto file_create_callback =
    +[](ntl::kmdf::device device, ntl::kmdf::request request,
        ntl::kmdf::file file) noexcept {
  try {
    auto &state = file.context<file_state>();
    state.name.assign(file.name());
    device.context<device_state>().open_files.fetch_add(
        1, std::memory_order_relaxed);

    const ntl::file wdm_file = file.wdm();
    DbgPrint("[crtsys KMDF sample] file opened (%Iu chars, read=%u, "
             "write=%u)\n",
             state.name.size(), wdm_file.can_read() ? 1u : 0u,
             wdm_file.can_write() ? 1u : 0u);
    request.complete(STATUS_SUCCESS);
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
};

constexpr auto file_cleanup_callback = +[](ntl::kmdf::file file) noexcept {
  file.context<file_state>().cleaned_up = true;
  DbgPrint("[crtsys KMDF sample] file cleanup\n");
};

constexpr auto file_close_callback = +[](ntl::kmdf::file file) noexcept {
  if (!file.context<file_state>().cleaned_up)
    DbgPrint("[crtsys KMDF sample] close preceded cleanup unexpectedly\n");
  file.owner().context<device_state>().open_files.fetch_sub(
      1, std::memory_order_relaxed);
  DbgPrint("[crtsys KMDF sample] file close\n");
};

constexpr auto work_item_callback =
    +[](ntl::kmdf::work_item item) noexcept {
  item.parent().context<device_state>().work_items.fetch_add(
      1, std::memory_order_relaxed);
  DbgPrint("[crtsys KMDF sample] work item ran at IRQL %lu\n",
           static_cast<unsigned long>(ntl::current_irql()));
};

constexpr auto timer_callback = +[](ntl::kmdf::timer timer) noexcept {
  timer.parent().context<device_state>().timer_expirations.fetch_add(
      1, std::memory_order_relaxed);
  DbgPrint("[crtsys KMDF sample] timer expired at IRQL %lu\n",
           static_cast<unsigned long>(ntl::current_irql()));
};

constexpr auto device_control_callback =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG io_control_code) noexcept {
  if (io_control_code != kmdf_ntl_sample::transform_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  complete_transform(queue.owner().context<device_state>(),
                     std::move(request));
};

constexpr auto unload_callback = +[](ntl::kmdf::driver) noexcept {
  DbgPrint("[crtsys KMDF sample] unload at IRQL %lu\n",
           static_cast<unsigned long>(ntl::current_irql()));
};

ntl::status create_control_device(const ntl::kmdf::driver &driver) {
  auto init_result = ntl::kmdf::control_device_init::try_allocate(
      driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
  if (!init_result)
    return init_result.status();

  auto init = std::move(init_result).value();
  ntl::status status =
      init.assign_name(std::wstring(L"\\Device\\CrtSysKmdfNtlSample"));
  if (status.is_err())
    return status;

  init.device_type(FILE_DEVICE_UNKNOWN).io_type(WdfDeviceIoBuffered);

  ntl::kmdf::file_config<file_state> files;
  files.execution_level(WdfExecutionLevelPassive)
      .on_create<file_create_callback>()
      .on_cleanup<file_cleanup_callback>()
      .on_close<file_close_callback>();

  init.file_objects(files);

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  auto device_result = init.try_create<device_state>(&device_attributes);
  if (!device_result)
    return device_result.status();

  const auto device = std::move(device_result).value();
  auto identity = device.try_emplace_context<device_identity>(0x4B4D4446u);
  if (!identity)
    return identity.status();
  if (device.context<device_identity>().value != 0x4B4D4446u)
    return STATUS_DATA_ERROR;

  status = device.try_create_symbolic_link(
      std::wstring(L"\\DosDevices\\CrtSysKmdfNtlSample"));
  if (status.is_err())
    return status;

  auto work_config =
      ntl::kmdf::work_item_config::with_callback<work_item_callback>();
  work_config.automatic_serialization(false);
  auto work_item = ntl::kmdf::work_item::try_create(device, work_config);
  if (!work_item)
    return work_item.status();
  work_item->enqueue();
  work_item->flush();
  if (device.context<device_state>().work_items.load(
          std::memory_order_relaxed) != 1)
    return STATUS_DATA_ERROR;

  auto timer_config =
      ntl::kmdf::timer_config::one_shot<timer_callback>();
  timer_config.automatic_serialization(false);
  ntl::kmdf::object_attributes timer_attributes;
  timer_attributes.execution_level(WdfExecutionLevelPassive);
  auto timer =
      ntl::kmdf::timer::try_create(device, timer_config, &timer_attributes);
  if (!timer)
    return timer.status();
  (void)timer->start_after_ms(1);

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_device_control<device_control_callback>();

  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  auto queue_result =
      ntl::kmdf::io_queue::try_create(device, queue_config, &queue_attributes);
  if (!queue_result)
    return queue_result.status();

  device.finish_initializing();
  return STATUS_SUCCESS;
}

void complete_transform(device_state &state,
                        ntl::kmdf::request request) noexcept {
  try {
    if (!ntl::is_passive_level()) {
      request.complete(STATUS_INVALID_DEVICE_STATE);
      return;
    }

    auto input_result =
        request.try_input_buffer<kmdf_ntl_sample::transform_request>();
    if (!input_result) {
      request.complete(input_result.status());
      return;
    }

    auto output_result =
        request.try_output_buffer<kmdf_ntl_sample::transform_reply>();
    if (!output_result) {
      request.complete(output_result.status());
      return;
    }

    const auto &input = input_result.value();
    auto &output = output_result.value();
    std::vector<std::uint32_t> values{input->value, 1, 2, 3};
    const std::uint32_t transformed =
        std::accumulate(values.begin(), values.end(), std::uint32_t{0});
    state.transformed_values.push_back(transformed);
    const std::string message =
        std::format("KMDF transformed {} to {}", input->value, transformed);

    *output = {};
    output->value = transformed;
    output->server_irql = static_cast<std::uint32_t>(ntl::current_irql());
    const size_t message_size =
        (std::min)(message.size(), sizeof(output->message) - 1);
    std::copy_n(message.data(), message_size, output->message);
    output->message[message_size] = '\0';

    request.complete(STATUS_SUCCESS, sizeof(kmdf_ntl_sample::transform_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
}

} // namespace

ntl::status ntl::kmdf::main(driver_builder &builder,
                            const std::wstring &registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  kmdf::driver_config config;
  config.non_pnp().on_unload<unload_callback>();

  kmdf::object_attributes attributes;
  attributes.execution_level(WdfExecutionLevelPassive);

  auto driver_result = builder.try_create(config, &attributes);
  if (!driver_result)
    return driver_result.status();

  return create_control_device(driver_result.value());
}
