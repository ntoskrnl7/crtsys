#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "kmdf_pnp_ntl_ioctl.hpp"

namespace {

struct device_state {
  std::atomic<std::uint32_t> io_count{0};
  std::atomic<std::uint32_t> prepare_count{0};
  std::atomic<std::uint32_t> d0_entry_count{0};
  std::atomic<bool> hardware_prepared{false};
  std::atomic<bool> in_d0{false};
};

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list raw,
        ntl::kmdf::resource_list translated) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  state.prepare_count.fetch_add(1, std::memory_order_relaxed);
  state.hardware_prepared.store(true, std::memory_order_release);
  for (const ntl::kmdf::resource_descriptor resource : translated) {
    DbgPrint("[crtsys KMDF PnP sample] resource: type=%u flags=0x%04x\n",
             static_cast<unsigned>(resource.type()),
             static_cast<unsigned>(resource.flags()));
  }
  DbgPrint(
      "[crtsys KMDF PnP sample] prepare hardware: raw=%lu translated=%lu\n",
      static_cast<unsigned long>(raw.size()),
      static_cast<unsigned long>(translated.size()));
  return STATUS_SUCCESS;
};

constexpr auto release_hardware =
    +[](ntl::kmdf::device device,
        ntl::kmdf::resource_list translated) noexcept -> NTSTATUS {
  device.context<device_state>().hardware_prepared.store(
      false, std::memory_order_release);
  DbgPrint("[crtsys KMDF PnP sample] release hardware: translated=%lu\n",
           static_cast<unsigned long>(translated.size()));
  return STATUS_SUCCESS;
};

constexpr auto d0_entry =
    +[](ntl::kmdf::device device,
        WDF_POWER_DEVICE_STATE previous_state) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  state.d0_entry_count.fetch_add(1, std::memory_order_relaxed);
  state.in_d0.store(true, std::memory_order_release);
  DbgPrint("[crtsys KMDF PnP sample] D0 entry: previous=%u\n",
           static_cast<unsigned>(previous_state));
  return STATUS_SUCCESS;
};

constexpr auto d0_exit =
    +[](ntl::kmdf::device device,
        WDF_POWER_DEVICE_STATE target_state) noexcept -> NTSTATUS {
  device.context<device_state>().in_d0.store(false,
                                             std::memory_order_release);
  DbgPrint("[crtsys KMDF PnP sample] D0 exit: target=%u\n",
           static_cast<unsigned>(target_state));
  return STATUS_SUCCESS;
};

void complete_query(device_state &state,
                    ntl::kmdf::request request) noexcept {
  try {
    if (!ntl::is_passive_level() ||
        !state.hardware_prepared.load(std::memory_order_acquire) ||
        !state.in_d0.load(std::memory_order_acquire)) {
      request.complete(STATUS_INVALID_DEVICE_STATE);
      return;
    }

    auto input_result =
        request.try_input_buffer<kmdf_pnp_ntl_sample::query_request>();
    if (!input_result) {
      request.complete(input_result.status());
      return;
    }

    auto output_result =
        request.try_output_buffer<kmdf_pnp_ntl_sample::query_reply>();
    if (!output_result) {
      request.complete(output_result.status());
      return;
    }

    const auto &input = input_result.value();
    auto &output = output_result.value();

    const std::vector<std::uint32_t> terms{input->value, 10, 20, 30};
    const std::uint32_t result =
        std::accumulate(terms.begin(), terms.end(), std::uint32_t{0});
    const std::uint32_t io_count =
        state.io_count.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::string message =
        std::format("PnP device transformed {} to {}", input->value, result);

    *output = {};
    output->value = result;
    output->io_count = io_count;
    output->prepare_count =
        state.prepare_count.load(std::memory_order_relaxed);
    output->d0_entry_count =
        state.d0_entry_count.load(std::memory_order_relaxed);
    output->server_irql = static_cast<std::uint32_t>(ntl::current_irql());
    const std::size_t message_size =
        (std::min)(message.size(), sizeof(output->message) - 1);
    std::copy_n(message.data(), message_size, output->message);
    output->message[message_size] = '\0';

    request.complete(STATUS_SUCCESS,
                     sizeof(kmdf_pnp_ntl_sample::query_reply));
  } catch (const std::bad_alloc &) {
    request.complete(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    request.complete(STATUS_UNSUCCESSFUL);
  }
}

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t,
        size_t, ULONG control_code) noexcept {
  if (control_code != kmdf_pnp_ntl_sample::query_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  complete_query(queue.owner().context<device_state>(), std::move(request));
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

  ntl::kmdf::object_attributes device_attributes;
  device_attributes.execution_level(WdfExecutionLevelPassive);
  auto created = init.try_create<device_state>(&device_attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  ntl::kmdf::idle_policy idle(IdleCannotWakeFromS0);
  idle.timeout(10'000, DriverManagedIdleTimeout)
      .user_control(IdleDoNotAllowUserControl)
      .enabled(true)
      .exclude_d3_cold(WdfUseDefault);
  ntl::status status = idle.try_apply(device);
  if (status.is_err())
    return status;

  status =
      device.try_create_interface(kmdf_pnp_ntl_sample::device_interface_guid);
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_device_control<device_control>();
  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  auto queue = ntl::kmdf::io_queue::try_create(device, queue_config,
                                                &queue_attributes);
  if (!queue)
    return queue.status();

  DbgPrint("[crtsys KMDF PnP sample] device added\n");
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
  auto driver = builder.try_create(config, &attributes);
  return driver ? ntl::status::ok() : driver.status();
}
