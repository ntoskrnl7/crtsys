#include <ntddk.h>

#include <ntl/kmdf/all>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

namespace sample_hardware {

constexpr ULONG interrupt_dma_complete = 0x00000001;
constexpr ULONG command_start_host_to_device = 0x00000001;
constexpr size_t maximum_transfer = 1024 * 1024;
constexpr size_t maximum_fragments = 64;

struct dma_descriptor {
  ULONGLONG address;
  ULONG length;
  ULONG reserved;
};

// Replace this illustrative layout with the target device's register map.
struct registers {
  ULONG interrupt_status;
  ULONG interrupt_acknowledge;
  ULONG descriptor_address_low;
  ULONG descriptor_address_high;
  ULONG descriptor_count;
  ULONG command;
};

} // namespace sample_hardware

struct device_state {
  ntl::kmdf::dma_enabler dma;
  ntl::kmdf::common_buffer descriptors;
  ntl::kmdf::dma_transaction transaction;
  volatile sample_hardware::registers *registers = nullptr;
  size_t register_length = 0;
  std::atomic<WDFREQUEST> active_request{WDF_NO_HANDLE};
};

// This state is shared with DISPATCH_LEVEL callbacks. A non-lock-free atomic
// could enter a runtime lock path that is not valid there, so reject such a
// target at compile time instead of assuming pointer atomics are lock-free.
static_assert(std::atomic<WDFREQUEST>::is_always_lock_free,
              "DMA request state must be lock-free at DISPATCH_LEVEL.");

void finish_request(device_state &state, NTSTATUS status,
                    size_t information) noexcept {
  WDFREQUEST native_request =
      state.active_request.exchange(WDF_NO_HANDLE, std::memory_order_acq_rel);
  if (native_request != WDF_NO_HANDLE)
    ntl::kmdf::request{native_request}.complete(status, information);
}

constexpr auto program_dma =
    +[](ntl::kmdf::dma_transaction, ntl::kmdf::device device,
        void *context, WDF_DMA_DIRECTION direction,
        ntl::kmdf::scatter_gather_list fragments) noexcept {
  // KMDF invokes EvtProgramDma at DISPATCH_LEVEL. Keep this callback and all
  // data it touches nonpageable. Do not use the PASSIVE_LEVEL CRT/STL surface;
  // any NTL, WDF, WDK, or standard-library operation added here must explicitly
  // document support for DISPATCH_LEVEL.
  auto &state = *static_cast<device_state *>(context);
  if (direction != WdfDmaDirectionWriteToDevice || !state.registers ||
      fragments.size() == 0 ||
      fragments.size() > sample_hardware::maximum_fragments)
    return false;

  auto *table = state.descriptors.data<sample_hardware::dma_descriptor>();
  for (ULONG index = 0; index != fragments.size(); ++index) {
    const SCATTER_GATHER_ELEMENT *fragment = fragments.at(index);
    table[index] = {static_cast<ULONGLONG>(fragment->Address.QuadPart),
                    fragment->Length, 0};
  }

  const PHYSICAL_ADDRESS table_address = state.descriptors.logical_address();
  WRITE_REGISTER_ULONG(&state.registers->descriptor_address_low,
                       table_address.LowPart);
  WRITE_REGISTER_ULONG(&state.registers->descriptor_address_high,
                       table_address.HighPart);
  WRITE_REGISTER_ULONG(&state.registers->descriptor_count, fragments.size());
  KeMemoryBarrier();
  WRITE_REGISTER_ULONG(&state.registers->command,
                       sample_hardware::command_start_host_to_device);

  UNREFERENCED_PARAMETER(device);
  return true;
};

constexpr auto interrupt_isr =
    +[](ntl::kmdf::interrupt interrupt, ULONG) noexcept {
  // This callback runs at DIRQL. Only access device registers and call APIs
  // explicitly permitted at DIRQL; do not use CRT/STL code here.
  auto &state = interrupt.owner().context<device_state>();
  if (!state.registers)
    return false;

  const ULONG status = READ_REGISTER_ULONG(&state.registers->interrupt_status);
  if ((status & sample_hardware::interrupt_dma_complete) == 0)
    return false;

  WRITE_REGISTER_ULONG(&state.registers->interrupt_acknowledge,
                       sample_hardware::interrupt_dma_complete);
  (void)interrupt.queue_dpc();
  return true;
};

constexpr auto interrupt_dpc =
    +[](ntl::kmdf::interrupt interrupt, ntl::kmdf::object) noexcept {
  // This DPC runs at DISPATCH_LEVEL. The DMA completion, lock-free request
  // state, and request completion APIs used below are selected for that IRQL.
  // Do not add general CRT/STL work unless its DISPATCH_LEVEL contract has
  // been separately established.
  auto &state = interrupt.owner().context<device_state>();
  const ntl::kmdf::dma_completion_result completion =
      state.transaction.complete_transfer();
  if (!completion.transaction_complete)
    return; // WDF calls program_dma again for the next fragment group.

  const size_t transferred = state.transaction.bytes_transferred();
  const NTSTATUS completion_status = completion.status;
  const ntl::status release_status = state.transaction.try_release();
  if (NT_SUCCESS(completion_status) && release_status.is_err()) {
    finish_request(state, release_status, 0);
    return;
  }

  finish_request(state, completion_status,
                 NT_SUCCESS(completion_status) ? transferred : 0);
};

constexpr auto prepare_hardware =
    +[](ntl::kmdf::device device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list translated) noexcept -> NTSTATUS {
  // EvtDevicePrepareHardware runs at PASSIVE_LEVEL. Pageable initialization
  // and explicitly PASSIVE_LEVEL CRT/STL work belongs in callbacks like this,
  // rather than in EvtProgramDma, the ISR, or the interrupt DPC.
  auto &state = device.context<device_state>();
  for (const ntl::kmdf::resource_descriptor resource : translated) {
    const auto memory = resource.memory();
    if (!memory || memory->length < sizeof(sample_hardware::registers) ||
        memory->length > MAXULONG_PTR)
      continue;

    const SIZE_T length = static_cast<SIZE_T>(memory->length);
    state.registers = static_cast<volatile sample_hardware::registers *>(
        MmMapIoSpace(memory->start, length, MmNonCached));
    if (!state.registers)
      return STATUS_INSUFFICIENT_RESOURCES;

    state.register_length = length;
    return STATUS_SUCCESS;
  }

  return STATUS_DEVICE_CONFIGURATION_ERROR;
};

constexpr auto release_hardware =
    +[](ntl::kmdf::device device,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  auto &state = device.context<device_state>();
  if (state.registers) {
    MmUnmapIoSpace(const_cast<sample_hardware::registers *>(state.registers),
                   state.register_length);
    state.registers = nullptr;
    state.register_length = 0;
  }
  return STATUS_SUCCESS;
};

constexpr auto write_request =
    +[](ntl::kmdf::io_queue queue, ntl::kmdf::request request,
        size_t) noexcept {
  // This sample deliberately uses only APIs valid through DISPATCH_LEVEL.
  // Configure the queue with WdfExecutionLevelPassive before adding ordinary
  // CRT/STL processing that requires PASSIVE_LEVEL.
  auto &state = queue.owner().context<device_state>();
  WDFREQUEST expected = WDF_NO_HANDLE;
  if (!state.active_request.compare_exchange_strong(
          expected, request.native_handle(), std::memory_order_acq_rel)) {
    request.complete(STATUS_DEVICE_BUSY);
    return;
  }

  ntl::status status = state.transaction.try_initialize_request<program_dma>(
      request, WdfDmaDirectionWriteToDevice);
  if (status.is_err()) {
    state.active_request.store(WDF_NO_HANDLE, std::memory_order_release);
    request.complete(status);
    return;
  }

  status = state.transaction.try_execute(&state);
  if (status.is_err()) {
    const ntl::status release_status = state.transaction.try_release();
    state.active_request.store(WDF_NO_HANDLE, std::memory_order_release);
    request.complete(release_status.is_err() ? static_cast<NTSTATUS>(release_status)
                                             : static_cast<NTSTATUS>(status));
  }
};

constexpr auto device_add =
    +[](ntl::kmdf::driver, ntl::kmdf::device_init &init) noexcept -> NTSTATUS {
  init.io_type(WdfDeviceIoDirect).power_not_pageable();

  ntl::kmdf::pnp_power_callbacks pnp;
  pnp.on_prepare_hardware<prepare_hardware>()
      .on_release_hardware<release_hardware>();
  init.pnp_power(pnp);

  auto created = init.try_create<device_state>();
  if (!created)
    return created.status();

  const ntl::kmdf::device device = created.value();
  auto &state = device.context<device_state>();

  ntl::kmdf::dma_enabler_config dma_config(
      WdfDmaProfileScatterGather64Duplex,
      sample_hardware::maximum_transfer);
  auto dma = ntl::kmdf::dma_enabler::try_create(device, dma_config);
  if (!dma)
    return dma.status();
  state.dma = dma.value();

  ntl::kmdf::common_buffer_config descriptor_alignment(15);
  auto descriptors = ntl::kmdf::common_buffer::try_create(
      state.dma,
      sizeof(sample_hardware::dma_descriptor) *
          sample_hardware::maximum_fragments,
      descriptor_alignment);
  if (!descriptors)
    return descriptors.status();
  state.descriptors = descriptors.value();

  auto transaction = ntl::kmdf::dma_transaction::try_create(state.dma);
  if (!transaction)
    return transaction.status();
  state.transaction = transaction.value();

  auto interrupt_config =
      ntl::kmdf::interrupt_config::with_isr<interrupt_isr>();
  interrupt_config.on_dpc<interrupt_dpc>();
  auto interrupt =
      ntl::kmdf::interrupt::try_create(device, interrupt_config);
  if (!interrupt)
    return interrupt.status();

  ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config.on_write<write_request>();
  auto queue = ntl::kmdf::io_queue::try_create(device, queue_config);
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
  auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
