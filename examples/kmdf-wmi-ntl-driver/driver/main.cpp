#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <atomic>
#include <cstdint>
#include <string>

#include "kmdf_wmi_ntl.hpp"

namespace {

struct device_state {
  ntl::kmdf::wmi_instance data_instance;
  ntl::kmdf::wmi_instance event_instance;
  std::atomic<bool> event_enabled{false};
  std::atomic<bool> instance_enabled{false};
};

struct instance_state {
  std::atomic<std::uint32_t> value{7};
  std::atomic<std::uint32_t> query_count{0};
  std::atomic<std::uint32_t> set_count{0};
  std::atomic<std::uint32_t> method_count{0};
  std::atomic<std::uint32_t> event_count{0};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "WMI callbacks require lock-free sample counters.");

constexpr auto provider_control =
    +[](ntl::kmdf::wmi_provider provider, WDF_WMI_PROVIDER_CONTROL control,
        bool enabled) noexcept -> NTSTATUS {
  auto &state = provider.owner().context<device_state>();
  if (control == WdfWmiEventControl)
    state.event_enabled.store(enabled, std::memory_order_release);
  else if (control == WdfWmiInstanceControl)
    state.instance_enabled.store(enabled, std::memory_order_release);
  return STATUS_SUCCESS;
};

constexpr auto query_instance =
    +[](ntl::kmdf::wmi_instance instance,
        ntl::kmdf::wmi_output_buffer output) noexcept -> NTSTATUS {
  auto &state = instance.context<instance_state>();
  const kmdf_wmi_ntl_sample::data_block data{
      state.value.load(std::memory_order_acquire),
      state.query_count.fetch_add(1, std::memory_order_relaxed) + 1,
      state.set_count.load(std::memory_order_relaxed),
      state.method_count.load(std::memory_order_relaxed),
      state.event_count.load(std::memory_order_relaxed)};
  return output.try_write(data);
};

constexpr auto set_instance =
    +[](ntl::kmdf::wmi_instance instance,
        ntl::kmdf::wmi_input_buffer input) noexcept -> NTSTATUS {
  const auto data = input.try_read<kmdf_wmi_ntl_sample::data_block>();
  if (!data)
    return data.status();

  auto &state = instance.context<instance_state>();
  state.value.store(data.value()->value, std::memory_order_release);
  state.set_count.fetch_add(1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto set_item =
    +[](ntl::kmdf::wmi_instance instance, ULONG item_id,
        ntl::kmdf::wmi_input_buffer input) noexcept -> NTSTATUS {
  if (item_id != kmdf_wmi_ntl_sample::value_item_id)
    return STATUS_WMI_READ_ONLY;

  const auto value = input.try_read<std::uint32_t>();
  if (!value)
    return value.status();

  auto &state = instance.context<instance_state>();
  state.value.store(*value.value(), std::memory_order_release);
  state.set_count.fetch_add(1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto execute_method =
    +[](ntl::kmdf::wmi_instance instance, ULONG method_id,
        ntl::kmdf::wmi_method_buffer buffer) noexcept -> NTSTATUS {
  if (method_id != kmdf_wmi_ntl_sample::transform_method_id)
    return STATUS_WMI_ITEMID_NOT_FOUND;

  const auto input =
      buffer.input().try_read<kmdf_wmi_ntl_sample::transform_input>();
  if (!input)
    return input.status();

  const kmdf_wmi_ntl_sample::transform_input input_copy = *input.value();
  const kmdf_wmi_ntl_sample::transform_output output{input_copy.value * 2};
  const ntl::status status = buffer.output().try_write(output);
  if (status.is_ok())
    instance.context<instance_state>().method_count.fetch_add(
        1, std::memory_order_relaxed);
  return status;
};

constexpr auto device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t, size_t,
        ULONG control_code) noexcept {
      if (control_code != kmdf_wmi_ntl_sample::trigger_event_ioctl_code) {
        request.complete(STATUS_INVALID_DEVICE_REQUEST);
        return;
      }

      auto output =
          request.try_output_buffer<kmdf_wmi_ntl_sample::trigger_event_reply>();
      if (!output) {
        request.complete(output.status());
        return;
      }

      auto &device = queue.owner().context<device_state>();
      if (!device.event_instance) {
        request.complete(STATUS_INVALID_DEVICE_STATE);
        return;
      }

      if (!device.data_instance) {
        request.complete(STATUS_INVALID_DEVICE_STATE);
        return;
      }

      auto &state = device.data_instance.context<instance_state>();
      const std::uint32_t sequence =
          state.event_count.fetch_add(1, std::memory_order_relaxed) + 1;
      const kmdf_wmi_ntl_sample::event_data event{sequence};
      const ntl::status status = device.event_instance.try_fire_event(event);
      if (status.is_err()) {
        request.complete(status);
        return;
      }

      output.value()->sequence = sequence;
      request.complete(STATUS_SUCCESS,
                       sizeof(kmdf_wmi_ntl_sample::trigger_event_reply));
    };

ntl::status configure_wmi(ntl::kmdf::device device) noexcept {
  ntl::status status = device.try_assign_mof_resource(
      std::wstring{kmdf_wmi_ntl_sample::mof_resource_name});
  if (status.is_err())
    return status;

  ntl::kmdf::wmi_provider_config data_provider_config(
      kmdf_wmi_ntl_sample::data_guid);
  data_provider_config
      .minimum_instance_buffer_size(sizeof(kmdf_wmi_ntl_sample::data_block))
      .expensive()
      .on_function_control<provider_control>();

  auto data_provider =
      ntl::kmdf::wmi_provider::try_create(device, data_provider_config);
  if (!data_provider)
    return data_provider.status();

  ntl::kmdf::wmi_instance_config data_instance_config(data_provider.value());
  data_instance_config.register_automatically()
      .on_query<query_instance>()
      .on_set<set_instance>()
      .on_set_item<set_item>()
      .on_execute<execute_method>();

  ntl::kmdf::object_attributes instance_attributes;
  auto data_instance = ntl::kmdf::wmi_instance::try_create<instance_state>(
      device, data_instance_config, &instance_attributes);
  if (!data_instance)
    return data_instance.status();

  if (data_instance->owner().native_handle() != device.native_handle() ||
      data_instance->provider().native_object() !=
          data_provider->native_object())
    return STATUS_INVALID_DEVICE_STATE;

  ntl::kmdf::wmi_provider_config event_provider_config(
      kmdf_wmi_ntl_sample::event_guid);
  event_provider_config.event_only();

  // KMDF creates a single event-only provider together with its instance.
  // WdfWmiProviderCreate is not the creation path for this configuration.
  ntl::kmdf::wmi_instance_config event_instance_config(event_provider_config);
  event_instance_config.register_automatically();

  ntl::kmdf::object_attributes event_instance_attributes;
  auto event_instance = ntl::kmdf::wmi_instance::try_create<instance_state>(
      device, event_instance_config, &event_instance_attributes);
  if (!event_instance)
    return event_instance.status();

  auto &state = device.context<device_state>();
  state.data_instance = data_instance.value();
  state.event_instance = event_instance.value();
  return ntl::status::ok();
}

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  try {
    init.device_type(FILE_DEVICE_UNKNOWN)
        .io_type(WdfDeviceIoBuffered)
        .power_pageable();

    ntl::kmdf::object_attributes device_attributes;
    device_attributes.execution_level(WdfExecutionLevelPassive);
    auto created = init.try_create<device_state>(&device_attributes);
    if (!created)
      return created.status();

    const ntl::kmdf::device device = created.value();
    ntl::status status =
        device.try_create_interface(kmdf_wmi_ntl_sample::device_interface_guid);
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

    status = configure_wmi(device);
    if (status.is_err())
      return status;

    DbgPrint("[crtsys KMDF WMI sample] device and providers created\n");
    return STATUS_SUCCESS;
  } catch (const std::bad_alloc &) {
    return STATUS_INSUFFICIENT_RESOURCES;
  } catch (...) {
    return STATUS_UNSUCCESSFUL;
  }
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
  if (driver)
    return ntl::status::ok();
  return driver.status();
}
