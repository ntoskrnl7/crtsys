#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include "kmdf_bus_ntl_ioctl.hpp"

namespace {

struct child_state {
  explicit child_state(std::uint32_t serial_number) noexcept
      : serial_number(serial_number) {}

  std::uint32_t serial_number;
  std::atomic<std::uint32_t> request_count{0};
  std::atomic<std::uint32_t> resource_requirements_queries{0};
  std::atomic<std::uint32_t> resource_queries{0};
  std::atomic<std::uint32_t> ejects{0};
  std::atomic<std::uint32_t> lock_changes{0};
  std::atomic<std::uint32_t> wake_enables{0};
  std::atomic<std::uint32_t> wake_disables{0};
  std::atomic<std::uint32_t> reported_missing{0};
};

NTSTATUS NTAPI get_serial_number(void *context,
                                 std::uint32_t *serial_number) noexcept {
  if (!context || !serial_number)
    return STATUS_INVALID_PARAMETER;
  const ntl::kmdf::pdo child{static_cast<WDFDEVICE>(context)};
  *serial_number = child.context<child_state>().serial_number;
  return STATUS_SUCCESS;
}

NTSTATUS NTAPI get_pdo_event_counts(
    void *context, kmdf_bus_ntl_sample::pdo_event_counts *counts) noexcept {
  if (!context || !counts)
    return STATUS_INVALID_PARAMETER;
  const ntl::kmdf::pdo child{static_cast<WDFDEVICE>(context)};
  const auto &state = child.context<child_state>();
  *counts = {
      state.resource_requirements_queries.load(std::memory_order_relaxed),
      state.resource_queries.load(std::memory_order_relaxed),
      state.ejects.load(std::memory_order_relaxed),
      state.lock_changes.load(std::memory_order_relaxed),
      state.wake_enables.load(std::memory_order_relaxed),
      state.wake_disables.load(std::memory_order_relaxed),
      state.reported_missing.load(std::memory_order_relaxed),
  };
  return STATUS_SUCCESS;
}

constexpr auto query_resource_requirements =
    +[](ntl::kmdf::pdo child,
        ntl::kmdf::io_resource_requirements requirements) noexcept -> NTSTATUS {
  child.context<child_state>().resource_requirements_queries.fetch_add(
      1, std::memory_order_relaxed);
  requirements.interface_type(Internal).slot_number(
      child.context<child_state>().serial_number);

  // This is a virtual child with no physical ranges to arbitrate. An empty
  // logical configuration expresses that requirement while exercising the
  // normal WDF requirements-list flow.
  auto configuration = requirements.try_create_list();
  if (!configuration)
    return configuration.status();
  return requirements.try_append(configuration.value());
};

constexpr auto query_boot_resources =
    +[](ntl::kmdf::pdo child,
        ntl::kmdf::resource_list resources) noexcept -> NTSTATUS {
  child.context<child_state>().resource_queries.fetch_add(
      1, std::memory_order_relaxed);
  // Firmware assigned no boot resources to this software-enumerated child.
  return resources.size() == 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
};

constexpr auto eject_child = +[](ntl::kmdf::pdo child) noexcept -> NTSTATUS {
  child.context<child_state>().ejects.fetch_add(1, std::memory_order_relaxed);
  DbgPrint("[crtsys KMDF bus sample] child PDO eject requested\n");
  return STATUS_SUCCESS;
};

constexpr auto set_child_lock =
    +[](ntl::kmdf::pdo child, bool locked) noexcept -> NTSTATUS {
  UNREFERENCED_PARAMETER(locked);
  child.context<child_state>().lock_changes.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto enable_child_wake =
    +[](ntl::kmdf::pdo child, SYSTEM_POWER_STATE state) noexcept -> NTSTATUS {
  UNREFERENCED_PARAMETER(state);
  child.context<child_state>().wake_enables.fetch_add(
      1, std::memory_order_relaxed);
  return STATUS_SUCCESS;
};

constexpr auto disable_child_wake = +[](ntl::kmdf::pdo child) noexcept {
  child.context<child_state>().wake_disables.fetch_add(
      1, std::memory_order_relaxed);
};

constexpr auto child_reported_missing = +[](ntl::kmdf::pdo child) noexcept {
  child.context<child_state>().reported_missing.fetch_add(
      1, std::memory_order_relaxed);
  DbgPrint("[crtsys KMDF bus sample] child PDO reported missing\n");
};

NTSTATUS NTAPI next_request_count(void *context,
                                  std::uint32_t *request_count) noexcept {
  if (!context || !request_count)
    return STATUS_INVALID_PARAMETER;
  const ntl::kmdf::pdo child{static_cast<WDFDEVICE>(context)};
  *request_count = child.context<child_state>().request_count.fetch_add(
                       1, std::memory_order_relaxed) +
                   1;
  return STATUS_SUCCESS;
}

constexpr auto bus_device_control =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request, size_t, size_t,
        ULONG control_code) noexcept {
      if (control_code != kmdf_bus_ntl_sample::child_action_ioctl_code) {
        request.complete(STATUS_INVALID_DEVICE_REQUEST);
        return;
      }

      auto input =
          request.try_input_buffer<kmdf_bus_ntl_sample::child_action_request>();
      if (!input) {
        request.complete(input.status());
        return;
      }
      auto output =
          request.try_output_buffer<kmdf_bus_ntl_sample::child_action_reply>();
      if (!output) {
        request.complete(output.status());
        return;
      }

      const auto action = input.value()->action;
      const std::uint32_t serial_number = input.value()->serial_number;
      const ntl::kmdf::child_identification<kmdf_bus_ntl_sample::child_identity>
          child{{serial_number}};
      const ntl::kmdf::child_list children =
          ntl::kmdf::child_list::default_for(queue.owner());

      NTSTATUS operation_status = STATUS_INVALID_PARAMETER;
      switch (action) {
      case kmdf_bus_ntl_sample::child_action::plug_in:
        operation_status = children.add_or_update(child);
        break;
      case kmdf_bus_ntl_sample::child_action::mark_missing:
        operation_status = children.mark_missing(child);
        break;
      case kmdf_bus_ntl_sample::child_action::eject:
        operation_status =
            children.request_eject(child) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        break;
      default:
        break;
      }

      *output.value() = {static_cast<std::int32_t>(operation_status),
                         serial_number};
      request.complete(STATUS_SUCCESS,
                       sizeof(kmdf_bus_ntl_sample::child_action_reply));
    };

constexpr auto create_child =
    +[](ntl::kmdf::child_list list,
        const ntl::kmdf::child_identification<
            kmdf_bus_ntl_sample::child_identity> &identification,
        ntl::kmdf::pdo_init init) noexcept -> NTSTATUS {
  try {
    ntl::status status = init.assign_device_id(L"CrtSys\\NtlChild");
    if (status.is_err())
      return status;
    status = init.assign_instance_id(
        std::format(L"{}", identification.payload.serial_number));
    if (status.is_err())
      return status;
    status = init.add_hardware_id(L"CrtSys\\NtlChild");
    if (status.is_err())
      return status;
    status = init.add_compatible_id(L"CrtSys\\NtlChildV1");
    if (status.is_err())
      return status;
    status = init.add_device_text(L"crtsys NTL child device",
                                  L"crtsys virtual bus", 0x0409);
    if (status.is_err())
      return status;
    init.default_locale(0x0409)
        .device_type(FILE_DEVICE_UNKNOWN)
        .io_type(WdfDeviceIoBuffered);

    ntl::kmdf::pdo_event_callbacks events;
    events.on_resource_requirements_query<query_resource_requirements>()
        .on_resources_query<query_boot_resources>()
        .on_eject<eject_child>()
        .on_set_lock<set_child_lock>()
        .on_enable_wake_at_bus<enable_child_wake>()
        .on_disable_wake_at_bus<disable_child_wake>()
        .on_reported_missing<child_reported_missing>();
    init.events(events);

    ntl::kmdf::object_attributes child_attributes;
    child_attributes.execution_level(WdfExecutionLevelPassive);
    auto created = init.try_create<child_state>(
        &child_attributes, identification.payload.serial_number);
    if (!created)
      return created.status();

    ntl::kmdf::pdo child = created.value();
    WDF_DEVICE_PNP_CAPABILITIES capabilities{};
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&capabilities);
    capabilities.Removable = WdfTrue;
    capabilities.EjectSupported = WdfTrue;
    capabilities.SurpriseRemovalOK = WdfTrue;
    child.pnp_capabilities(capabilities);

    auto interface = ntl::kmdf::make_query_interface<
        kmdf_bus_ntl_sample::child_query_interface>(
        1, child.native_handle(), ntl::kmdf::reference_query_interface_object,
        ntl::kmdf::dereference_query_interface_object);
    interface.get_serial_number = get_serial_number;
    interface.next_request_count = next_request_count;
    interface.get_pdo_event_counts = get_pdo_event_counts;
    ntl::kmdf::query_interface_config interface_config{
        interface, kmdf_bus_ntl_sample::child_query_interface_guid};
    status = child.try_add_query_interface(interface_config);
    if (status.is_err())
      return status;

    ntl::kmdf::child_identification<kmdf_bus_ntl_sample::child_identity>
        round_trip;
    status = child.retrieve_identification(round_trip);
    if (status.is_err() ||
        round_trip.payload.serial_number !=
            identification.payload.serial_number ||
        !child.parent() ||
        child.parent().native_handle() != list.owner().native_handle())
      return STATUS_DATA_ERROR;

    DbgPrint("[crtsys KMDF bus sample] child PDO %lu created\n",
             static_cast<unsigned long>(identification.payload.serial_number));
    return STATUS_SUCCESS;
  } catch (const std::bad_alloc &) {
    return STATUS_INSUFFICIENT_RESOURCES;
  } catch (...) {
    return STATUS_UNSUCCESSFUL;
  }
};

constexpr auto compare_child =
    +[](ntl::kmdf::child_list,
        const ntl::kmdf::child_identification<
            kmdf_bus_ntl_sample::child_identity> &left,
        const ntl::kmdf::child_identification<
            kmdf_bus_ntl_sample::child_identity> &right) noexcept {
      return left.payload.serial_number == right.payload.serial_number;
    };

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  using child_config =
      ntl::kmdf::child_list_config<kmdf_bus_ntl_sample::child_identity>;
  auto children = child_config::with_create<create_child>();
  children.on_compare<compare_child>();

  init.device_type(FILE_DEVICE_BUS_EXTENDER)
      .io_type(WdfDeviceIoBuffered)
      .power_pageable()
      .default_child_list(children);

  ntl::kmdf::object_attributes attributes;
  attributes.execution_level(WdfExecutionLevelPassive);
  auto created = init.try_create(&attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device bus = created.value();
  PNP_BUS_INFORMATION information{};
  information.BusTypeGuid = kmdf_bus_ntl_sample::internal_bus_type_guid;
  information.LegacyBusType = Internal;
  information.BusNumber = 0;
  bus.bus_information(information);

  ntl::status status =
      bus.try_create_interface(kmdf_bus_ntl_sample::bus_interface_guid);
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_device_control<bus_device_control>();
  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  auto queue =
      ntl::kmdf::io_queue::try_create(bus, queue_config, &queue_attributes);
  if (!queue)
    return queue.status();

  DbgPrint("[crtsys KMDF bus sample] virtual bus added\n");
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
