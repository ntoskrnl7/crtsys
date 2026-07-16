#include <ntddk.h>
#include <wdmsec.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <vector>

#include "ioctl.hpp"

namespace {

struct device_state {
  ntl::kmdf::io_queue pending_requests;
  std::mutex transformed_values_lock;
  std::vector<std::uint32_t> transformed_values;
  std::atomic<std::uint32_t> open_files{0};
  std::atomic<std::uint32_t> released_requests{0};
  std::atomic<std::uint32_t> canceled_requests{0};
  std::atomic<std::uint32_t> transformed_requests{0};
  std::atomic<std::uint32_t> forward_progress_reservations{0};
  std::atomic<std::uint32_t> forward_progress_requests{0};
};

struct file_state {
  bool cleaned_up = false;
};

constexpr auto file_create_callback =
    +[](ntl::kmdf::device device, ntl::kmdf::request request,
        ntl::kmdf::file) noexcept {
      device.context<device_state>().open_files.fetch_add(
          1, std::memory_order_relaxed);
      request.complete(STATUS_SUCCESS);
    };

constexpr auto file_cleanup_callback = +[](ntl::kmdf::file file) noexcept {
  file.context<file_state>().cleaned_up = true;
};

constexpr auto file_close_callback = +[](ntl::kmdf::file file) noexcept {
  if (!file.context<file_state>().cleaned_up) {
    DbgPrint("[crtsys KMDF stress] close without cleanup\n");
  }
  file.owner().context<device_state>().open_files.fetch_sub(
      1, std::memory_order_relaxed);
};

void complete_transform(device_state &state,
                        ntl::kmdf::request request) noexcept {
  try {
    if (!ntl::is_passive_level()) {
      request.complete(STATUS_INVALID_DEVICE_STATE);
      return;
    }

    auto input =
        request.try_input_buffer<kmdf_verifier_stress::transform_request>();
    auto output =
        request.try_output_buffer<kmdf_verifier_stress::transform_reply>();
    if (!input || !output) {
      request.complete(!input ? input.status() : output.status());
      return;
    }

    const std::vector<std::uint32_t> values{input.value()->value, 1, 2, 3};
    const std::uint32_t transformed =
        std::accumulate(values.begin(), values.end(), std::uint32_t{0});
    {
      std::lock_guard guard(state.transformed_values_lock);
      state.transformed_values.push_back(transformed);
    }

    output.value()->value = transformed;
    output.value()->server_irql =
        static_cast<std::uint32_t>(ntl::current_irql());
    state.transformed_requests.fetch_add(1, std::memory_order_relaxed);
    request.complete(STATUS_SUCCESS,
                     sizeof(kmdf_verifier_stress::transform_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
}

constexpr auto device_control_callback =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG code) noexcept {
      auto &state = queue.owner().context<device_state>();

      if (code == kmdf_verifier_stress::transform_ioctl) {
        complete_transform(state, std::move(request));
        return;
      }

      if (code == kmdf_verifier_stress::wait_ioctl) {
        const ntl::status status =
            std::move(request).try_forward_to(state.pending_requests);
        if (status.is_err())
          request.complete(status);
        return;
      }

      if (code == kmdf_verifier_stress::release_ioctl) {
        const ntl::kmdf::file associated_file = request.associated_file();
        auto found = state.pending_requests.try_find(nullptr, &associated_file);
        if (!found) {
          request.complete(found.status());
          return;
        }

        auto pending =
            state.pending_requests.try_retrieve(std::move(found).value());
        if (!pending) {
          request.complete(pending.status());
          return;
        }

        auto input =
            pending->try_input_buffer<kmdf_verifier_stress::wait_request>();
        auto output =
            pending->try_output_buffer<kmdf_verifier_stress::wait_reply>();
        if (!input || !output) {
          const ntl::status status =
              !input ? input.status() : output.status();
          pending->complete(status);
          request.complete(status);
          return;
        }

        output.value()->value = input.value()->value + 1000;
        output.value()->server_irql =
            static_cast<std::uint32_t>(ntl::current_irql());
        pending->complete(STATUS_SUCCESS,
                          sizeof(kmdf_verifier_stress::wait_reply));
        state.released_requests.fetch_add(1, std::memory_order_relaxed);
        request.complete(STATUS_SUCCESS);
        return;
      }

      if (code == kmdf_verifier_stress::stats_ioctl) {
        auto output =
            request.try_output_buffer<kmdf_verifier_stress::queue_stats>();
        if (!output) {
          request.complete(output.status());
          return;
        }

        ULONG queued = 0;
        (void)state.pending_requests.state(&queued, nullptr);
        *output.value() = {
            queued,
            state.released_requests.load(std::memory_order_relaxed),
            state.canceled_requests.load(std::memory_order_relaxed),
            state.transformed_requests.load(std::memory_order_relaxed),
            state.open_files.load(std::memory_order_relaxed),
            state.forward_progress_requests.load(std::memory_order_relaxed)};
        request.complete(STATUS_SUCCESS,
                         sizeof(kmdf_verifier_stress::queue_stats));
        return;
      }

      request.complete(STATUS_INVALID_DEVICE_REQUEST);
    };

constexpr auto canceled_callback =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request) noexcept {
      queue.owner().context<device_state>().canceled_requests.fetch_add(
          1, std::memory_order_relaxed);
      request.complete(STATUS_CANCELLED);
    };

constexpr auto reserved_callback =
    +[](ntl::kmdf::io_queue queue,
        ntl::kmdf::reserved_request_resources) noexcept -> NTSTATUS {
  queue.owner().context<device_state>().forward_progress_reservations.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto request_resources_callback =
    +[](ntl::kmdf::io_queue queue,
        ntl::kmdf::request_resources) noexcept -> NTSTATUS {
  queue.owner().context<device_state>().forward_progress_requests.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

ntl::status create_device(const ntl::kmdf::driver &driver) {
  auto init_result = ntl::kmdf::control_device_init::try_allocate(
      driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
  if (!init_result)
    return init_result.status();

  auto init = std::move(init_result).value();
  ntl::status status =
      init.assign_name(L"\\Device\\CrtSysKmdfVerifierStress");
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
  const ntl::kmdf::device device = device_result.value();

  status = device.try_create_symbolic_link(
      L"\\DosDevices\\CrtSysKmdfVerifierStress");
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config manual_config(WdfIoQueueDispatchManual, false);
  manual_config.on_canceled<canceled_callback>();
  ntl::kmdf::object_attributes manual_attributes;
  manual_attributes.execution_level(WdfExecutionLevelPassive);
  auto manual = ntl::kmdf::io_queue::try_create(
      device, manual_config, &manual_attributes);
  if (!manual)
    return manual.status();
  device.context<device_state>().pending_requests = manual.value();

  ntl::kmdf::io_queue_config default_config(WdfIoQueueDispatchParallel);
  default_config.on_device_control<device_control_callback>();
  ntl::kmdf::object_attributes default_attributes;
  default_attributes.execution_level(WdfExecutionLevelPassive)
      .synchronization_scope(WdfSynchronizationScopeNone);
  auto default_queue = ntl::kmdf::io_queue::try_create(
      device, default_config, &default_attributes);
  if (!default_queue)
    return default_queue.status();

  auto forward_progress =
      ntl::kmdf::forward_progress_policy::always_reserved(1);
  forward_progress.prepare_reserved_requests<reserved_callback>()
      .prepare_each_request<request_resources_callback>();
  status = forward_progress.try_assign(default_queue.value());
  if (status.is_err())
    return status;
  if (device.context<device_state>()
          .forward_progress_reservations.load(std::memory_order_relaxed) != 1)
    return STATUS_DATA_ERROR;

  device.finish_initializing();
  return STATUS_SUCCESS;
}

constexpr auto unload_callback = +[](ntl::kmdf::driver) noexcept {
  DbgPrint("[crtsys KMDF stress] unloaded\n");
};

} // namespace

ntl::status ntl::kmdf::main(driver_builder &builder,
                            const std::wstring &registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  kmdf::driver_config config;
  config.non_pnp().on_unload<unload_callback>();
  kmdf::object_attributes attributes;
  attributes.execution_level(WdfExecutionLevelPassive);
  auto driver = builder.try_create(config, &attributes);
  if (!driver)
    return driver.status();
  return create_device(driver.value());
}
