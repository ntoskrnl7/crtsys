#include <ntddk.h>
#include <wdmsec.h>

#include <ntl/event>
#include <ntl/irql>
#include <ntl/kmdf/all>
#include <ntl/wait>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include "kmdf_ntl_ioctl.hpp"

namespace {

struct device_state {
  std::vector<std::uint32_t> transformed_values;
  ntl::kmdf::io_queue pending_requests;
  std::atomic<std::uint32_t> open_files{0};
  std::atomic<std::uint32_t> work_items{0};
  std::atomic<std::uint32_t> timer_expirations{0};
  std::atomic<std::uint32_t> released_requests{0};
  std::atomic<std::uint32_t> canceled_requests{0};

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

struct utility_dpc_state {
  std::atomic<std::uint32_t> executions{0};
  std::atomic<std::uint32_t> observed_irql{0};
  ntl::event completed{ntl::event_type::synchronization};
};

struct utility_wait_lock_state {
  explicit utility_wait_lock_state(ntl::kmdf::wait_lock lock) noexcept
      : lock(lock) {}

  ntl::kmdf::wait_lock lock;
  ntl::event acquired{ntl::event_type::synchronization};
  ntl::event release{ntl::event_type::synchronization};
};

ntl::event utility_object_destroyed{ntl::event_type::synchronization};

struct utility_object_state {
  explicit utility_object_state(std::uint32_t value) noexcept : value(value) {}

  ~utility_object_state() noexcept { utility_object_destroyed.set(); }

  std::uint32_t value;
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

constexpr auto utility_dpc_callback = +[](ntl::kmdf::dpc work) noexcept {
  // Standalone KMDF DPC callbacks run at DISPATCH_LEVEL. Keep this path
  // allocation-free and do not use the general PASSIVE_LEVEL CRT/STL surface.
  auto &state = work.context<utility_dpc_state>();
  state.observed_irql.store(static_cast<std::uint32_t>(ntl::current_irql()),
                            std::memory_order_relaxed);
  state.executions.fetch_add(1, std::memory_order_relaxed);
  state.completed.set();
};

constexpr auto utility_wait_lock_callback =
    +[](ntl::kmdf::work_item work) noexcept {
      auto &state = work.context<utility_wait_lock_state>();
      state.lock.lock();
      state.acquired.set();
      (void)state.release.wait();
      state.lock.unlock();
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

ntl::status verify_request_memory_surface(ntl::kmdf::device device) noexcept {
  ntl::kmdf::object_attributes memory_attributes;
  memory_attributes.parent(device);

  auto allocated = ntl::kmdf::memory::try_allocate(
      sizeof(std::uint32_t) * 4, NonPagedPoolNx, "NTLm",
      &memory_attributes);
  if (!allocated)
    return allocated.status();

  constexpr std::array<std::uint32_t, 4> source{10, 20, 30, 40};
  std::array<std::uint32_t, 4> destination{};
  ntl::status status =
      allocated->try_copy_from(0, source.data(), sizeof(source));
  if (status.is_err())
    return status;
  status = allocated->try_copy_to(0, destination.data(), sizeof(destination));
  if (status.is_err())
    return status;
  if (destination != source ||
      allocated->size<std::uint32_t>() != source.size())
    return STATUS_DATA_ERROR;

  auto target = ntl::kmdf::io_target::try_create(device);
  if (!target)
    return target.status();
  if (target->owner().native_handle() != device.native_handle())
    return STATUS_DATA_ERROR;

  // Driver-created requests are deleted by driver_request when not sent.
  auto request =
      ntl::kmdf::driver_request::try_create(device.default_io_target());
  if (!request)
    return request.status();

  DbgPrint("[crtsys KMDF sample] request/memory surface verified\n");
  return STATUS_SUCCESS;
}

ntl::status verify_common_object_surface(ntl::kmdf::device device) noexcept {
  utility_object_destroyed.clear();
  {
    ntl::kmdf::object_attributes object_config;
    object_config.parent(device);
    auto general =
        ntl::kmdf::owned_object::try_create<utility_object_state>(
            &object_config, 42);
    if (!general)
      return general.status();
    if (general->context<utility_object_state>().value != 42)
      return STATUS_DATA_ERROR;

    static ULONG reference_tag = ntl::pool_tag("NTLo");
    auto reference = general->reference(&reference_tag);
    auto moved_reference = std::move(reference);
    if (reference || !moved_reference ||
        moved_reference.context<utility_object_state>().value != 42)
      return STATUS_DATA_ERROR;
    moved_reference.reset();

    ntl::kmdf::object_reference device_reference{device};
    if (!device_reference)
      return STATUS_DATA_ERROR;
  }

  ntl::status status = ntl::wait_for(utility_object_destroyed, 1000);
  if (status.is_err())
    return status;

  ntl::kmdf::object_attributes spin_attributes;
  spin_attributes.parent(device);
  auto spin = ntl::kmdf::spin_lock::try_create(&spin_attributes);
  if (!spin)
    return spin.status();

  ULONG protected_value = 0;
  {
    // WDF spin-lock acquisition raises IRQL to DISPATCH_LEVEL. Only the scalar
    // state update belongs inside this critical section.
    std::lock_guard<ntl::kmdf::spin_lock> guard(*spin);
    ++protected_value;
  }

  ntl::kmdf::object_attributes wait_attributes;
  wait_attributes.parent(device);
  auto wait = ntl::kmdf::wait_lock::try_create(&wait_attributes);
  if (!wait)
    return wait.status();
  {
    std::lock_guard<ntl::kmdf::wait_lock> guard(*wait);
    ++protected_value;
  }
  if (!wait->try_lock())
    return STATUS_DEVICE_BUSY;
  wait->unlock();

  auto wait_work_config =
      ntl::kmdf::work_item_config::with_callback<utility_wait_lock_callback>();
  auto wait_work = ntl::kmdf::work_item::try_create<utility_wait_lock_state>(
      device, wait_work_config, nullptr, *wait);
  if (!wait_work)
    return wait_work.status();
  wait_work->enqueue();

  auto &wait_state = wait_work->context<utility_wait_lock_state>();
  status = ntl::wait_for(wait_state.acquired, 1000);
  if (static_cast<NTSTATUS>(status) != STATUS_SUCCESS) {
    wait_state.release.set();
    wait_work->flush();
    return status;
  }

  const bool acquired_while_contended = wait->try_lock();
  wait_state.release.set();
  wait_work->flush();
  if (acquired_while_contended)
    return STATUS_DATA_ERROR;

  ntl::kmdf::object_attributes lookaside_attributes;
  lookaside_attributes.parent(device);
  auto cache = ntl::kmdf::lookaside::try_create(
      sizeof(std::array<std::uint32_t, 4>), NonPagedPoolNx, "NTLk",
      &lookaside_attributes);
  if (!cache)
    return cache.status();

  auto allocation = cache->try_allocate();
  if (!allocation)
    return allocation.status();
  constexpr std::array<std::uint32_t, 4> source{1, 3, 5, 7};
  std::array<std::uint32_t, 4> destination{};
  status = allocation->try_copy_from(0, source.data(), sizeof(source));
  if (status.is_err())
    return status;
  status = allocation->try_copy_to(0, destination.data(), sizeof(destination));
  if (status.is_err() || destination != source)
    return status.is_err() ? status : ntl::status{STATUS_DATA_ERROR};

  ntl::kmdf::object_attributes string_attributes;
  string_attributes.parent(device);
  constexpr std::wstring_view expected_text = L"NTL KMDF object utilities";
  auto text = ntl::kmdf::string::try_create(expected_text, &string_attributes);
  if (!text)
    return text.status();
  if (text->view() != expected_text)
    return STATUS_DATA_ERROR;

  ntl::kmdf::object_attributes collection_attributes;
  collection_attributes.parent(device);
  auto objects = ntl::kmdf::collection::try_create(&collection_attributes);
  if (!objects)
    return objects.status();
  status = objects->try_add(*text);
  if (status.is_err())
    return status;
  status = objects->try_add(*cache);
  if (status.is_err())
    return status;
  if (objects->size() != 2 ||
      objects->first().native_handle() !=
          reinterpret_cast<WDFOBJECT>(text->native_object()) ||
      objects->last().native_handle() !=
          reinterpret_cast<WDFOBJECT>(cache->native_object()))
    return STATUS_DATA_ERROR;
  objects->remove(*text);
  objects->remove_at(0);
  if (!objects->empty())
    return STATUS_DATA_ERROR;

  auto dpc_config =
      ntl::kmdf::dpc_config::with_callback<utility_dpc_callback>();
  dpc_config.automatic_serialization(false);
  auto deferred = ntl::kmdf::dpc::try_create<utility_dpc_state>(
      device, dpc_config, nullptr);
  if (!deferred)
    return deferred.status();
  if (!deferred->enqueue())
    return STATUS_DEVICE_BUSY;

  status =
      ntl::wait_for(deferred->context<utility_dpc_state>().completed, 1000);
  if (status.is_err())
    return status;
  const auto &dpc_state = deferred->context<utility_dpc_state>();
  if (dpc_state.executions.load(std::memory_order_relaxed) != 1 ||
      dpc_state.observed_irql.load(std::memory_order_relaxed) !=
          DISPATCH_LEVEL ||
      protected_value != 2)
    return STATUS_DATA_ERROR;

  DbgPrint("[crtsys KMDF sample] common object utilities verified\n");
  return STATUS_SUCCESS;
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
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t, size_t,
        ULONG io_control_code) noexcept {
      auto &state = queue.owner().context<device_state>();

      if (io_control_code == kmdf_ntl_sample::transform_ioctl_code) {
        complete_transform(state, std::move(request));
        return;
      }

      if (io_control_code == kmdf_ntl_sample::manual_wait_ioctl_code) {
        const ntl::status status =
            std::move(request).try_forward_to(state.pending_requests);
        if (status.is_err())
          request.complete(status);
        return;
      }

      if (io_control_code == kmdf_ntl_sample::manual_release_ioctl_code) {
        const ntl::kmdf::file associated_file = request.associated_file();
        ntl::kmdf::request_parameters parameters;
        auto found = state.pending_requests.try_find(nullptr, &associated_file,
                                                     &parameters);
        if (!found) {
          request.complete(found.status());
          return;
        }
        if (parameters.type() != WdfRequestTypeDeviceControl ||
            parameters.value().Parameters.DeviceIoControl.IoControlCode !=
                kmdf_ntl_sample::manual_wait_ioctl_code) {
          request.complete(STATUS_OBJECT_TYPE_MISMATCH);
          return;
        }

        auto pending =
            state.pending_requests.try_retrieve(std::move(found).value());
        if (!pending) {
          request.complete(pending.status());
          return;
        }

        auto input =
            pending->try_input_buffer<kmdf_ntl_sample::manual_wait_request>();
        auto output =
            pending->try_output_buffer<kmdf_ntl_sample::manual_wait_reply>();
        if (!input || !output) {
          const NTSTATUS status = !input ? input.status() : output.status();
          pending->complete(status);
          request.complete(status);
          return;
        }

        const auto &input_buffer = input.value();
        auto &output_buffer = output.value();
        output_buffer->value = input_buffer->value + 1000;
        output_buffer->server_irql =
            static_cast<std::uint32_t>(ntl::current_irql());
        pending->complete(STATUS_SUCCESS,
                          sizeof(kmdf_ntl_sample::manual_wait_reply));
        state.released_requests.fetch_add(1, std::memory_order_relaxed);
        request.complete(STATUS_SUCCESS);
        return;
      }

      if (io_control_code == kmdf_ntl_sample::manual_stats_ioctl_code) {
        auto output =
            request.try_output_buffer<kmdf_ntl_sample::manual_queue_stats>();
        if (!output) {
          request.complete(output.status());
          return;
        }

        auto &output_buffer = output.value();
        ULONG queued_requests = 0;
        (void)state.pending_requests.state(&queued_requests, nullptr);
        output_buffer->queued_requests = queued_requests;
        output_buffer->released_requests =
            state.released_requests.load(std::memory_order_relaxed);
        output_buffer->canceled_requests =
            state.canceled_requests.load(std::memory_order_relaxed);
        request.complete(STATUS_SUCCESS,
                         sizeof(kmdf_ntl_sample::manual_queue_stats));
        return;
      }

      {
        request.complete(STATUS_INVALID_DEVICE_REQUEST);
      }
    };

constexpr auto manual_queue_canceled_callback =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request) noexcept {
      queue.owner().context<device_state>().canceled_requests.fetch_add(
          1, std::memory_order_relaxed);
      request.complete(STATUS_CANCELLED);
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

  status = verify_request_memory_surface(device);
  if (status.is_err())
    return status;

  status = verify_common_object_surface(device);
  if (status.is_err())
    return status;

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

  ntl::kmdf::io_queue_config manual_queue_config(WdfIoQueueDispatchManual,
                                                 false);
  manual_queue_config.on_canceled<manual_queue_canceled_callback>();

  ntl::kmdf::object_attributes manual_queue_attributes;
  manual_queue_attributes.execution_level(WdfExecutionLevelPassive);
  auto manual_queue = ntl::kmdf::io_queue::try_create(
      device, manual_queue_config, &manual_queue_attributes);
  if (!manual_queue)
    return manual_queue.status();
  device.context<device_state>().pending_requests = manual_queue.value();

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
