#include <ntddk.h>

#include <ntl/kmdf/all>

#include <atomic>
#include <cstdint>
#include <string>

#include "kmdf_usb_ntl_ioctl.hpp"

namespace {

struct device_state {
  ntl::kmdf::usb_device usb;
  ntl::kmdf::usb_interface interface;
  ntl::kmdf::usb_pipe input_pipe;
  ntl::kmdf::usb_pipe output_pipe;
  USB_DEVICE_DESCRIPTOR descriptor{};
  std::atomic<std::uint32_t> continuous_reads{0};
  std::uint8_t configured_pipe_count = 0;
  std::uint8_t input_endpoint = 0;
  std::uint8_t output_endpoint = 0;
  std::uint32_t input_packet_size = 0;
  std::uint32_t output_packet_size = 0;
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "USB reader callbacks require lock-free counters");

constexpr auto reader_completion = +[](ntl::kmdf::usb_pipe, ntl::kmdf::memory,
                                       size_t, void *context) noexcept {
  // USB reader completions normally run at DISPATCH_LEVEL. Keep this path
  // nonpageable and restricted to operations documented for that IRQL. Do not
  // use the PASSIVE_LEVEL CRT/STL surface here; queue a passive work item when
  // the received data requires general C++ processing.
  auto &state = *static_cast<device_state *>(context);
  state.continuous_reads.fetch_add(1, std::memory_order_relaxed);
};

constexpr auto reader_failed = +[](ntl::kmdf::usb_pipe, ntl::status status,
                                   USBD_STATUS usbd_status) noexcept {
  UNREFERENCED_PARAMETER(status);
  UNREFERENCED_PARAMETER(usbd_status);

  // WDF invokes the failure callback at PASSIVE_LEVEL. Return true only when
  // retrying is valid for the real device protocol. This generic template
  // leaves the reader stopped so recovery policy cannot loop indefinitely.
  return false;
};

ntl::status configure_usb(ntl::kmdf::device device) noexcept {
  auto &state = device.context<device_state>();

  auto created = ntl::kmdf::usb_device::try_create(device);
  if (!created)
    return created.status();
  state.usb = created.value();
  state.descriptor = state.usb.descriptor();

  auto selection = ntl::kmdf::usb_select_config::single_interface();
  ntl::status status = state.usb.try_select(selection);
  if (status.is_err())
    return status;

  state.interface = selection.configured_interface();
  state.configured_pipe_count = state.interface.configured_pipe_count();

  for (UCHAR index = 0; index < state.configured_pipe_count; ++index) {
    ntl::kmdf::usb_pipe_information information;
    const auto pipe = state.interface.pipe_at(index, &information);
    const bool stream_pipe = information.type() == WdfUsbPipeTypeBulk ||
                             information.type() == WdfUsbPipeTypeInterrupt;
    if (!stream_pipe)
      continue;

    if (!state.input_pipe && information.is_in()) {
      state.input_pipe = pipe;
      state.input_endpoint = information.endpoint_address();
      state.input_packet_size = information.maximum_packet_size();
    } else if (!state.output_pipe && information.is_out()) {
      state.output_pipe = pipe;
      state.output_endpoint = information.endpoint_address();
      state.output_packet_size = information.maximum_packet_size();
    }
  }

  if (!state.input_pipe)
    return STATUS_NOT_FOUND;

  auto reader = ntl::kmdf::usb_continuous_reader_config::with_completion<
      reader_completion>(state.input_packet_size, &state);
  reader.on_failure<reader_failed>();
  return state.input_pipe.try_configure_reader(reader);
}

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  // WDF requires USB target creation/configuration at PASSIVE_LEVEL and
  // recommends configuring a continuous reader in EvtDevicePrepareHardware.
  return configure_usb(device);
};

constexpr auto release_hardware =
    +[](ntl::kmdf::device device,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  state.output_pipe = {};
  state.input_pipe = {};
  state.interface = {};
  state.usb = {};
  return STATUS_SUCCESS;
};

constexpr auto d0_entry =
    +[](ntl::kmdf::device device, WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  if (!state.input_pipe)
    return STATUS_SUCCESS;
  return state.input_pipe.try_start();
};

constexpr auto d0_exit =
    +[](ntl::kmdf::device device, WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  if (state.input_pipe)
    state.input_pipe.stop(WdfIoTargetCancelSentIo);
  return STATUS_SUCCESS;
};

constexpr auto device_control = +[](ntl::kmdf::io_queue queue,
                                    ntl::kmdf::request request, size_t, size_t,
                                    ULONG code) noexcept {
  if (code != kmdf_usb_ntl_sample::query_ioctl_code) {
    request.complete(STATUS_INVALID_DEVICE_REQUEST);
    return;
  }

  auto output = request.try_output_buffer<kmdf_usb_ntl_sample::query_reply>();
  if (!output) {
    request.complete(output.status());
    return;
  }

  const auto &state = queue.owner().context<device_state>();
  *output.value() = {};
  output.value()->vendor_id = state.descriptor.idVendor;
  output.value()->product_id = state.descriptor.idProduct;
  output.value()->interface_count = state.usb.interface_count();
  output.value()->configured_pipe_count = state.configured_pipe_count;
  output.value()->input_endpoint = state.input_endpoint;
  output.value()->output_endpoint = state.output_endpoint;
  output.value()->input_packet_size = state.input_packet_size;
  output.value()->output_packet_size = state.output_packet_size;
  output.value()->continuous_read_count =
      state.continuous_reads.load(std::memory_order_relaxed);
  request.complete(STATUS_SUCCESS, sizeof(kmdf_usb_ntl_sample::query_reply));
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

  const auto device = created.value();
  ntl::status status =
      device.try_create_interface(kmdf_usb_ntl_sample::device_interface_guid);
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
