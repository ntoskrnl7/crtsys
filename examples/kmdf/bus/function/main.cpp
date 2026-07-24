#include <ntddk.h>

#include <ntl/irql>
#include <ntl/kmdf/all>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "kmdf_bus_ntl_ioctl.hpp"

namespace {

struct device_state {
  ntl::kmdf::queried_interface<kmdf_bus_ntl_sample::child_query_interface>
      bus_interface;
};

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  auto queried =
      device.try_query_interface<kmdf_bus_ntl_sample::child_query_interface>(
          kmdf_bus_ntl_sample::child_query_interface_guid, 1);
  if (!queried)
    return queried.status();

  auto interface = std::move(queried).value();
  if (!interface->get_serial_number || !interface->next_request_count ||
      !interface->get_pdo_event_counts)
    return STATUS_INVALID_DEVICE_STATE;

  std::uint32_t serial_number = 0;
  const NTSTATUS status =
      interface->get_serial_number(interface->header.Context, &serial_number);
  if (!NT_SUCCESS(status))
    return status;

  device.context<device_state>().bus_interface = std::move(interface);
  DbgPrint("[crtsys KMDF bus function] queried child %lu interface\n",
           static_cast<unsigned long>(serial_number));
  return STATUS_SUCCESS;
};

constexpr auto release_hardware =
    +[](ntl::kmdf::device device,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  device.context<device_state>().bus_interface.reset();
  return STATUS_SUCCESS;
};

constexpr auto device_control = +[](ntl::kmdf::io_queue queue,
                                    ntl::kmdf::request request, size_t, size_t,
                                    ULONG control_code) noexcept {
  if (control_code != kmdf_bus_ntl_sample::query_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  try {
    auto output = request.try_output_buffer<kmdf_bus_ntl_sample::query_reply>();
    if (!output) {
      request.complete(output.status());
      return;
    }

    auto &interface = queue.owner().context<device_state>().bus_interface;
    if (!interface) {
      request.complete(STATUS_INVALID_DEVICE_STATE);
      return;
    }

    std::uint32_t serial_number = 0;
    NTSTATUS status =
        interface->get_serial_number(interface->header.Context, &serial_number);
    if (!NT_SUCCESS(status)) {
      request.complete(status);
      return;
    }
    std::uint32_t request_count = 0;
    status = interface->next_request_count(interface->header.Context,
                                           &request_count);
    if (!NT_SUCCESS(status)) {
      request.complete(status);
      return;
    }
    kmdf_bus_ntl_sample::pdo_event_counts pdo_events{};
    status =
        interface->get_pdo_event_counts(interface->header.Context, &pdo_events);
    if (!NT_SUCCESS(status)) {
      request.complete(status);
      return;
    }

    const std::string message = std::format(
        "function driver called child {} query interface {} time(s)",
        serial_number, request_count);
    *output.value() = {};
    output.value()->serial_number = serial_number;
    output.value()->request_count = request_count;
    output.value()->server_irql =
        static_cast<std::uint32_t>(ntl::current_irql());
    output.value()->pdo_events = pdo_events;
    const std::size_t length =
        (std::min)(message.size(), sizeof(output.value()->message) - 1);
    std::copy_n(message.data(), length, output.value()->message);
    output.value()->message[length] = '\0';
    request.complete(STATUS_SUCCESS, sizeof(kmdf_bus_ntl_sample::query_reply));
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
      .on_release_hardware<release_hardware>();
  init.pnp_power(pnp);

  ntl::kmdf::object_attributes attributes;
  attributes.execution_level(WdfExecutionLevelPassive);
  auto created = init.try_create<device_state>(&attributes);
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  ntl::status status = device.try_create_interface(
      kmdf_bus_ntl_sample::child_device_interface_guid);
  if (status.is_err())
    return status;

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_device_control<device_control>();
  ntl::kmdf::object_attributes queue_attributes;
  queue_attributes.execution_level(WdfExecutionLevelPassive);
  auto queue =
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
  auto driver = builder.try_create(config, &attributes);
  return driver ? ntl::status::ok() : driver.status();
}
