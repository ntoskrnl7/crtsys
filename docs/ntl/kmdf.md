# KMDF Helpers

[Back to NTL documentation](./README.md)

NTL KMDF support is an optional C++ surface over the normal KMDF object model.
It does not replace WDF dispatch, PnP, power, queue, request, parent-child
lifetime, or synchronization rules.

## Select The Entry Model

Visual Studio/MSBuild projects keep `<DriverType>KMDF</DriverType>` and opt in:

```xml
<CrtSysUseNtlKmdfMain>true</CrtSysUseNtlKmdfMain>
```

CMake projects add `NTL` to the KMDF driver declaration:

```cmake
crtsys_add_driver(my_driver KMDF 1.15 NTL src/main.cpp)
```

Without that opt-in, a KMDF project keeps its standard `DriverEntry` and calls
`WdfDriverCreate` itself.

## Driver Entry

The NTL entry receives a `driver_builder` bound to the native `DriverEntry`
arguments. `try_create()` preserves the `NTSTATUS` returned by
`WdfDriverCreate`.

```cpp
#include <ntl/kmdf/all>

constexpr auto on_driver_unload =
    +[](kmdf::driver) noexcept {};

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring& registry_path) {
  (void)registry_path;

  kmdf::driver_config config;
  config.non_pnp().on_unload<on_driver_unload>();

  auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
```

`driver`, `device`, `file`, `io_queue`, `io_target`, `request`, `memory`,
`dma_enabler`, `common_buffer`, `dma_transaction`, `interrupt`, `timer`,
`work_item`, and `child_list` are non-owning facades.
WDF retains native object ownership and applies the configured parent-child
lifetime rules. `registry_key` and `driver_request` are move-only owners:
`registry_key` closes its `WDFKEY`, while `driver_request` deletes an unsent
driver-created `WDFREQUEST`.

## Plug And Play Device

`EvtDriverDeviceAdd` receives a framework-owned `PWDFDEVICE_INIT`.
`device_init` is deliberately non-owning: it never calls `WdfDeviceInitFree`.
On successful `try_create()`, `WdfDeviceCreate` consumes the initialization
object. If the callback returns before creating a device, KMDF deletes it after
the callback returns.

```cpp
constexpr auto on_prepare_hardware =
    +[](kmdf::device, kmdf::resource_list,
        kmdf::resource_list) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_release_hardware =
    +[](kmdf::device,
        kmdf::resource_list) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_d0_entry =
    +[](kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_d0_exit =
    +[](kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_device_control =
    +[](kmdf::io_queue, kmdf::request request, size_t, size_t,
        ULONG) noexcept {
      request.complete(STATUS_NOT_SUPPORTED);
    };

constexpr auto on_device_add =
    +[](kmdf::driver, kmdf::device_init& init) noexcept -> NTSTATUS {
      ntl::kmdf::pnp_power_callbacks callbacks;
      callbacks
          .on_prepare_hardware<on_prepare_hardware>()
          .on_release_hardware<on_release_hardware>()
          .on_d0_entry<on_d0_entry>()
          .on_d0_exit<on_d0_exit>();

      init.io_type(WdfDeviceIoBuffered).pnp_power(callbacks);

      ntl::kmdf::object_attributes attributes;
      attributes.execution_level(WdfExecutionLevelPassive);
      auto device = init.try_create(&attributes);
      if (!device)
        return device.status();

      ntl::status status =
          device.value().try_create_interface(GUID_DEVINTERFACE_MY_DEVICE);
      if (status.is_err())
        return status;

      ntl::kmdf::io_queue_config queue(WdfIoQueueDispatchSequential);
      queue.on_device_control<on_device_control>();
      queue.power_managed(WdfTrue);
      auto created_queue =
          ntl::kmdf::io_queue::try_create(device.value(), queue);
      return created_queue ? STATUS_SUCCESS : created_queue.status();
    };

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring&) {
  ntl::kmdf::driver_config config;
  config.on_device_add<on_device_add>();
  auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
```

The unary `+` converts each non-capturing lambda to a compile-time function
pointer. Giving that pointer a `constexpr` name before passing it as a template
argument also keeps Visual Studio IntelliSense from misparsing an inline lambda
template argument. NTL installs an allocation-free WDF thunk that converts
native handles to non-owning `driver`, `device`, `io_queue`, `request`, and
`resource_list` facades. Callbacks must be `noexcept`. Persistent state belongs
in a WDF object context because WDF callbacks cannot retain a C++ lambda closure.

`pnp_power_callbacks` covers hardware prepare/release, D0 transitions,
self-managed I/O, query-stop/query-remove, and surprise removal.
`power_policy_callbacks` covers S0/Sx wake policy callbacks. Both expose
`native()` for less common WDF fields rather than hiding the native framework.

The `device` facade also exposes operational state without inventing a second
state machine: `pnp_state()`, `power_state()`, `power_policy_state()`,
`system_power_action()`, `state()`, `set_failed()`, and
`indicate_wake_status()`. `default_queue()` and `try_route_requests()` cover
default and type-specific dispatch. `device_idle_reference::try_acquire()` is
a move-only RAII pairing of `WdfDeviceStopIdleNoTrack` and
`WdfDeviceResumeIdleNoTrack`; waiting for D0 requires `PASSIVE_LEVEL`.

## Hardware Resources

`resource_list` is an iterable, non-owning view of the resource list supplied
by KMDF. Each element is a `resource_descriptor`; its `memory()`, `port()`,
`interrupt()`, `dma()`, and `connection()` methods return typed values only
when the descriptor has the matching type. `native()` remains available for
less common resource types. Large memory descriptors are decoded with the WDK
`RtlCmDecodeMemIoResource` helper rather than by interpreting their compressed
length fields directly.

```cpp
constexpr auto on_prepare_hardware =
    +[](kmdf::device device, kmdf::resource_list raw,
        kmdf::resource_list translated) noexcept -> NTSTATUS {
      (void)raw;

      for (const kmdf::resource_descriptor resource : translated) {
        const auto memory = resource.memory();
        if (!memory)
          continue;

        auto* registers = MmMapIoSpace(memory->start,
                                       static_cast<SIZE_T>(memory->length),
                                       MmNonCached);
        if (!registers)
          return STATUS_INSUFFICIENT_RESOURCES;
        device.context<device_state>().registers = registers;
        return STATUS_SUCCESS;
      }
      return STATUS_DEVICE_CONFIGURATION_ERROR;
    };
```

The raw list contains bus-relative resources. The translated list contains
system addresses, interrupt vectors, and affinities suitable for the driver's
hardware-access path. `resource_origin` is carried into interrupt descriptors
so message-signaled interrupt fields are decoded from the correct native union
member. Resource facades do not own or extend the lifetime of the WDF list and
are valid only from `EvtDevicePrepareHardware` until
`EvtDeviceReleaseHardware` returns.

## Idle And Wake Policy

`idle_policy` initializes `WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS` with the WDF
initializer and configures runtime idle in S0. `wake_policy` does the same for
`WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS` and wake from Sx. Both return the exact
WDF `NTSTATUS` from `try_apply()`.

```cpp
ntl::kmdf::idle_policy idle(IdleCannotWakeFromS0);
idle.timeout(10'000, DriverManagedIdleTimeout)
    .user_control(IdleDoNotAllowUserControl)
    .enabled(true)
    .exclude_d3_cold(WdfUseDefault);

ntl::status status = idle.try_apply(device);
if (status.is_err())
  return status;

ntl::kmdf::wake_policy wake;
wake.device_state(PowerDeviceMaximum)
    .user_control(WakeDoNotAllowUserControl)
    .enabled(true);

status = wake.try_apply(device);
if (status.is_err())
  return status;
```

Apply these settings after `WdfDeviceCreate` and only when the driver owns the
device power policy. The wrappers follow the native WDF contract of at most
`DISPATCH_LEVEL`; the example configures them during `EvtDeviceAdd`, where the
driver is already running at `PASSIVE_LEVEL`. Whether a device can actually
wake from a given state remains a hardware, bus, firmware, and INF contract.

## C++ Object Context

NTL can construct a C++ object directly in WDF-owned context storage. This is
the KMDF counterpart of the extension lifetime managed by `ntl::device<T>`:
construction follows successful WDF object creation, and the destructor runs
from the WDF destroy callback before the context storage is released.

```cpp
struct device_state {
  std::vector<std::uint32_t> completed_values;
  std::atomic<std::uint32_t> open_files{0};

  ~device_state() noexcept = default;
};

ntl::kmdf::object_attributes attributes;
attributes.execution_level(WdfExecutionLevelPassive);

auto created = init.try_create<device_state>(&attributes);
if (!created)
  return created.status();

auto& state = created->context<device_state>();
```

Context constructors and destructors must be `noexcept`. WDF stores one
context for each C++ type; `object::try_emplace_context<T>()` can attach an
additional type after object creation. `try_context<T>()` returns null when
that type is absent, while `context<T>()` asserts that it is present. Native
`WDF_DECLARE_CONTEXT_TYPE*` contexts remain available through
`object_attributes::context_type()` when NTL-managed C++ lifetime is not used.

## Control Device And Queue

`control_device_init` owns a `PWDFDEVICE_INIT` until it is consumed by
`try_create()`. If setup fails first, its destructor calls
`WdfDeviceInitFree`. `object_attributes`, `driver_config`, and
`io_queue_config` install typed callbacks while retaining `native()` access to
the corresponding WDF configuration structures for fields not wrapped by NTL.

```cpp
#include <wdmsec.h>

auto init = ntl::kmdf::control_device_init::try_allocate(
    driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
if (!init)
  return init.status();

auto device = init.value().try_create();
if (!device)
  return device.status();

ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
queue_config.on_device_control<on_device_control>();

auto queue = ntl::kmdf::io_queue::try_create(device.value(), queue_config);
if (!queue)
  return queue.status();
```

For a complete non-PnP control-device flow with an application, typed request
buffers, STL use, symbolic-link creation, and queue callbacks, see the
[NTL KMDF sample](../../examples/kmdf-ntl-driver).

## File Objects

`ntl::kmdf::file` observes a `WDFFILEOBJECT`. Its `wdm()` method returns an
`ntl::file` view of the underlying `PFILE_OBJECT`; neither facade owns or
dereferences the object. Use `ntl::unique_object<PFILE_OBJECT>` when an
object-manager reference must be owned, and a handle owner for a file opened
through `ZwCreateFile`.

```cpp
struct file_state {
  std::wstring name;
  bool cleaned_up = false;
  ~file_state() noexcept = default;
};

constexpr auto on_file_create =
    +[](kmdf::device device, kmdf::request request,
        kmdf::file file) noexcept {
      try {
        file.context<file_state>().name.assign(file.name());
        ++device.context<device_state>().open_files;
        request.complete(STATUS_SUCCESS);
      } catch (...) {
        request.complete(STATUS_INSUFFICIENT_RESOURCES);
      }
    };

constexpr auto on_file_cleanup = +[](kmdf::file file) noexcept {
  file.context<file_state>().cleaned_up = true;
};

constexpr auto on_file_close = +[](kmdf::file file) noexcept {
  NT_ASSERT(file.context<file_state>().cleaned_up);
};

ntl::kmdf::file_config<file_state> files;
files
    .on_create<on_file_create>()
    .on_cleanup<on_file_cleanup>()
    .on_close<on_file_close>();

init.file_objects(files);
```

The file context is constructed before the typed create callback and destroyed
with the `WDFFILEOBJECT`, after the native cleanup/close sequence. Configure
file-object cleanup behavior with `file_config`, rather than combining an
NTL-managed file context with `object_attributes::on_cleanup()` or
`on_destroy()`.

## Request Buffers

`request::try_input_buffer<T>()` and `try_output_buffer<T>()` call the matching
KMDF buffer-retrieval APIs and return `ntl::result<request_buffer<T>>`.
`request_buffer<T>` is a non-owning view valid only under the native WDF request
buffer lifetime contract.

The request facade also exposes framework memory objects and MDLs with
`try_input_memory()`, `try_output_memory()`, `try_input_mdl()`, and
`try_output_mdl()`. The explicitly named `try_unsafe_user_*()` functions are
only for `EvtIoInCallerContext`; validate or lock those user addresses before
retaining them. `try_lock_user_buffer_for_read()` and
`try_lock_user_buffer_for_write()` return the resulting WDF memory object.

`parameters()`, `requestor_mode()`, `is_from_32bit_process()`,
`associated_queue()`, and `wdm_irp()` provide request diagnostics and explicit
WDM interoperation without duplicating WDF's request state.

`request` is move-only because a KMDF callback has one right to complete,
forward, requeue, or send the request. `request::try_forward_to()` and
`try_requeue()` are rvalue-qualified and empty the facade after WDF accepts the
transfer:

```cpp
auto status = std::move(request).try_forward_to(destination);
```

`try_mark_cancelable()` and `try_unmark_cancelable()` preserve KMDF's native
cancellation race: `STATUS_CANCELLED` from unmark means the cancellation
callback owns completion.

## Manual Queues And Cancellation

Create a non-default `WdfIoQueueDispatchManual` queue when requests must wait
for hardware, data, or another request. `try_retrieve_next()` and
`try_retrieve_for()` remove a request from that queue and return a move-only
`request`, transferring the right to process and complete it to the caller:

```cpp
ntl::kmdf::io_queue_config pending_config(
    WdfIoQueueDispatchManual, false);
pending_config.on_canceled<on_canceled>();

auto pending = ntl::kmdf::io_queue::try_create(
    device, pending_config, &passive_attributes);

auto waiting = pending->try_retrieve_for(release.associated_file());
if (!waiting)
  return waiting.status();

waiting->complete(STATUS_SUCCESS);
```

`try_find()` supports non-destructive inspection. KMDF increments the found
request's object reference but does not give the driver request ownership, so
NTL returns a move-only `found_request` that automatically dereferences the
object. Pass it to `try_retrieve()` to atomically attempt the ownership
transition. `STATUS_NOT_FOUND` means cancellation or another consumer removed
the request first. `request_parameters` supplies initialized storage for the
found request's native parameters.

Do not call `try_mark_cancelable()` while a request is still in a WDF queue;
the framework owns queued-request cancellation. An `on_canceled()` queue
callback receives the canceled request and must complete it. After a driver
retrieves and retains a request, it may mark it cancelable. Before completing
that driver-owned request outside the cancellation callback, call
`try_unmark_cancelable()`: a `STATUS_CANCELLED` result means the cancellation
callback owns the only legal completion.

To send a formatted request to another device stack, register an allocation-
free completion callback and transfer it to an `io_target`:

```cpp
auto target = device.default_io_target();
ntl::kmdf::send_options options;
options.timeout(WDF_REL_TIMEOUT_IN_MS(1000));

constexpr auto on_request_completion =
    +[](kmdf::request request, kmdf::io_target,
        kmdf::completion_params result, void*) noexcept {
      request.complete(result.status(), result.information());
    };
request.on_completion<on_request_completion>();

auto status = std::move(request).try_send(target, &options);
if (status.is_err()) {
  // WDF did not accept the send; this code still controls request.
  request.complete(status);
}
```

Successful send transfers control to WDF until the completion callback.
Failed send leaves the original request facade usable. `send_options`
supports both asynchronous transfer and synchronous waiting. A successful
asynchronous send empties the source `request`; a synchronous send retains it
so the caller can complete the original queue request after the lower target
finishes. Send-and-forget remains a native WDF escape hatch because it has a
different ownership contract.

## Queue Forward Progress

`forward_progress_policy` reserves framework requests so essential I/O can
still reach a queue when ordinary request allocation fails. Assign the policy
after creating the queue and before the device starts processing I/O:

```cpp
constexpr auto prepare_reserved_request =
    +[](kmdf::io_queue,
        kmdf::reserved_request_resources resources) noexcept -> NTSTATUS {
      configure_reserved_storage(resources.as_object());
      return STATUS_SUCCESS;
    };

constexpr auto prepare_each_request =
    +[](kmdf::io_queue,
        kmdf::request_resources resources) noexcept -> NTSTATUS {
      configure_request_storage(resources.as_object());
      return STATUS_SUCCESS;
    };

auto policy = kmdf::forward_progress_policy::always_reserved(2);
policy.prepare_reserved_requests<prepare_reserved_request>()
    .prepare_each_request<prepare_each_request>();

auto status = policy.try_assign(queue);
if (status.is_err())
  return status;
```

`always_reserved()` is the normal reliability policy. `paging_io()` selects
KMDF's paging-I/O policy, while `examine<Callback>()` lets a DISPATCH_LEVEL-safe
callback choose whether each IRP fails or consumes a reserved request.
`prepare_reserved_requests()` performs one-time preparation for every reserved
request and receives `reserved_request_resources`, whose type guarantees that
contract. In contrast, KMDF calls `prepare_each_request()` for every new
framework request after creating it and before inserting it into a queue,
including ordinary requests that are not reserved. Returning failure from that
callback rejects the request.

Both callbacks receive restricted resource-preparation views rather than a
normal `request`, so they cannot accidentally complete, forward, or requeue a
request that has not entered an I/O queue. They can run at `DISPATCH_LEVEL` and
must not call the PASSIVE_LEVEL CRT/STL surface.

## WDF Memory And Driver-Created Requests

`memory` wraps `WDFMEMORY`. `try_allocate()` creates WDF-owned storage and
`try_preallocated()` wraps caller-owned storage. The facade is non-owning, so
give long-lived memory an explicit WDF parent. A preallocated buffer must
outlive the WDF memory object that refers to it.

```cpp
ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto transfer = ntl::kmdf::memory::try_allocate(
    4096, NonPagedPoolNx, "Xfer", &attributes);
if (!transfer)
  return transfer.status();

std::array<std::byte, 16> header{};
auto status = transfer->try_copy_from(0, header.data(), header.size());
```

`request` represents a request delivered by a WDF queue and is completed or
forwarded. `driver_request` instead owns a request created by the driver with
`WdfRequestCreate`. Destroying an unsent `driver_request` calls
`WdfObjectDelete`; it must not call `request::complete()`.

```cpp
auto target = device.default_io_target();
auto created = ntl::kmdf::driver_request::try_create(target);
if (!created)
  return created.status();

auto outgoing = std::move(created).value();
auto status = outgoing.try_format_ioctl(
    target, IOCTL_SAMPLE_QUERY, input_memory, output_memory);
if (status.is_err())
  return status;

ntl::kmdf::send_options options;
options.timeout(WDF_REL_TIMEOUT_IN_MS(1000));
status = outgoing.try_allocate_timer();
if (status.is_err())
  return status;
return outgoing.try_send_and_wait(target, &options);
```

For asynchronous sends, `driver_request::try_send()` transfers ownership to
the completion path. The default overload deletes the request after
completion. A typed completion callback receives a new `driver_request` owner
and can inspect, reuse, and resend it; otherwise its destructor deletes it.
Synchronous and send-and-forget options are rejected by the asynchronous
overload so ownership cannot silently change.

The formatting surface covers read, write, IOCTL, internal IOCTL, and the
three-argument internal-IOCTL form. `try_allocate_timer()` exposes
`WdfRequestAllocateTimer` for reliable timed sends without a timer allocation
failure in the send path.

Drivers can create and open a non-default target with `io_target::try_create()`
and `io_target_open_params`. The target object remains WDF-owned:

```cpp
auto created_target = ntl::kmdf::io_target::try_create(device);
if (!created_target)
  return created_target.status();

ntl::unicode_string name(L"\\Device\\SampleTarget");
auto open = ntl::kmdf::io_target_open_params::open_by_name(
    &*name, GENERIC_READ | GENERIC_WRITE);
auto status = created_target->try_open(open);
```

For PASSIVE_LEVEL one-shot operations, `memory_descriptor` describes a caller
buffer, MDL, or WDF memory range. `io_target::try_read()`, `try_write()`,
`try_ioctl()`, `try_internal_ioctl()`, and `try_internal_ioctl_others()` use
KMDF's synchronous target helpers without requiring a separately created
request. The target facade also exposes target WDM device/file objects as
explicit `wdm_*()` interoperation methods.

## Common WDF Object Utilities

The KMDF namespace provides typed facades for framework spin locks, wait
locks, fixed-size lookaside memory, object collections, strings, and
standalone DPCs. These are distinct from similarly named WDM-oriented NTL
types: `ntl::kmdf::*` objects participate in WDF's parent hierarchy,
verification, callback serialization, and reference model.

The generic lifecycle helpers keep three different responsibilities explicit:

- `object` is a non-owning view of any `WDFOBJECT`.
- `owned_object` owns deletion of a general object created by
  `WdfObjectCreate` and calls `WdfObjectDelete` when reset or destroyed.
- `object_reference` owns one reference-count increment and balances it with
  `WdfObjectDereferenceActual`; it never requests deletion with
  `WdfObjectDelete`.

An `owned_object` must not outlive its configured WDF parent. Deleting it can
return before the final destruction callback if another `object_reference` is
still held. These types are also distinct from `ntl::unique_object`, which
owns an object-manager pointer reference and calls `ObDereferenceObject`.

```cpp
struct operation_state {
  explicit operation_state(ULONG id) noexcept : id(id) {}
  ULONG id;
};

ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto operation = ntl::kmdf::owned_object::try_create<operation_state>(
    &attributes, 42);
if (!operation)
  return operation.status();

auto pending_reference = operation->reference();
auto id = pending_reference.context<operation_state>().id;

// The same reference owner accepts typed WDF facades.
ntl::kmdf::object_reference device_reference{device};
```

`spin_lock` and `wait_lock` implement the standard C++ lockable surface, so
they work with `std::lock_guard`. A WDF spin lock raises IRQL to
`DISPATCH_LEVEL`; a blocking WDF wait-lock acquisition requires
`PASSIVE_LEVEL`. Keep PASSIVE_LEVEL-only CRT/STL work outside a spin-lock
critical section. `wait_lock::try_lock()` uses a zero timeout and returns
`false` for `STATUS_TIMEOUT`; that status is informational, so a generic
`NT_SUCCESS` check is not sufficient to decide whether the lock was acquired.

```cpp
auto spin = ntl::kmdf::spin_lock::try_create();
if (!spin)
  return spin.status();
{
  std::lock_guard guard(*spin);
  ++shared_counter; // nonpageable, DISPATCH_LEVEL-safe work only
}

auto wait = ntl::kmdf::wait_lock::try_create();
if (!wait)
  return wait.status();
{
  std::lock_guard guard(*wait); // blocking acquisition: PASSIVE_LEVEL
  update_passive_state();
}
```

`lookaside` creates fixed-size `WDFMEMORY` allocations. `try_allocate()`
returns a move-only `lookaside_memory`; its destructor calls
`WdfObjectDelete`, which returns the backing block to the WDF lookaside list.
This explicit owner prevents a successful allocation from being silently
leaked.

```cpp
ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto packets = ntl::kmdf::lookaside::try_create(
    sizeof(packet), NonPagedPoolNx, "Pkt0", &attributes);
if (!packets)
  return packets.status();

auto packet_memory = packets->try_allocate();
if (!packet_memory)
  return packet_memory.status();
auto* packet_view = packet_memory->data<packet>();
```

`collection` retains framework references to WDF objects until they are
removed or the collection is deleted. `string` copies a `UNICODE_STRING` or
`std::wstring_view` into a WDF-owned string object; creation and value access
require `PASSIVE_LEVEL`.

```cpp
auto label = ntl::kmdf::string::try_create(L"sample device");
auto objects = ntl::kmdf::collection::try_create();
if (!label || !objects)
  return STATUS_INSUFFICIENT_RESOURCES;
auto status = objects->try_add(*label);
```

A standalone `dpc` is parented to another WDF object and invokes its typed
callback at `DISPATCH_LEVEL`. `enqueue()` is valid through `HIGH_LEVEL`;
`cancel(true)` waits for a running callback and therefore requires
`PASSIVE_LEVEL`. Use only nonpageable, IRQL-safe operations in the callback
and defer general CRT/STL work to `ntl::kmdf::work_item`.

```cpp
constexpr auto on_dpc = +[](ntl::kmdf::dpc work) noexcept {
  work.context<dpc_state>().pending.store(true,
                                          std::memory_order_relaxed);
};

auto config = ntl::kmdf::dpc_config::with_callback<on_dpc>();
config.automatic_serialization(false);
auto deferred = ntl::kmdf::dpc::try_create<dpc_state>(
    device, config, nullptr);
if (!deferred)
  return deferred.status();
deferred->enqueue();
```

## DMA

`dma_enabler`, `common_buffer`, and `dma_transaction` preserve KMDF's native
DMA object and callback model while replacing repetitive configuration and
handle conversion with typed facades. Creating an enabler or common buffer is
a `PASSIVE_LEVEL` operation. The program-DMA callback normally runs at
`DISPATCH_LEVEL`, so it must not use the PASSIVE_LEVEL CRT/STL surface.

```cpp
ntl::kmdf::dma_enabler_config dma_config(
    WdfDmaProfileScatterGather64, 1024 * 1024);

auto dma = ntl::kmdf::dma_enabler::try_create(device, dma_config);
if (!dma)
  return dma.status();

ntl::kmdf::common_buffer_config aligned(15); // 16-byte alignment
auto descriptors = ntl::kmdf::common_buffer::try_create(
    dma.value(), 4096, aligned);
if (!descriptors)
  return descriptors.status();

auto* table = descriptors->data<device_descriptor>();
const auto device_address = descriptors->logical_address();
```

A request-backed DMA transaction installs an allocation-free typed program
callback. The callback receives the scatter/gather list that must be
programmed into the device:

```cpp
constexpr auto program_dma =
    +[](ntl::kmdf::dma_transaction transaction,
        ntl::kmdf::device,
        void* context,
        WDF_DMA_DIRECTION direction,
        ntl::kmdf::scatter_gather_list fragments) noexcept {
      auto& registers = *static_cast<device_registers*>(context);
      for (ULONG i = 0; i != fragments.size(); ++i) {
        const auto* fragment = fragments.at(i);
        registers.program(direction, fragment->Address,
                          fragment->Length);
      }
      return true;
    };

auto transaction = ntl::kmdf::dma_transaction::try_create(dma.value());
if (!transaction)
  return transaction.status();

auto status = transaction->try_initialize_request<program_dma>(
    request, WdfDmaDirectionWriteToDevice);
if (status.is_err())
  return status;

return transaction->try_execute(&registers);
```

The facade deliberately does not delete an active transaction. After the
interrupt/DPC path reports the last transfer with `complete_transfer()` or
`complete_final()`, call `try_release()` before reinitializing the same WDF
object, or call `destroy()` when it will not be reused. If `try_execute()`
fails after successful initialization, release the transaction before reuse.
The WDF parent remains the final teardown fallback.

Package builds instantiate these typed DMA callbacks on every supported
toolset and architecture. Runtime DMA execution requires matching hardware
resources and is therefore not claimed by the software-only VM smoke test.
The buildable [KMDF DMA driver template](../../examples/kmdf-dma-ntl-driver)
shows the complete request, scatter/gather programming, interrupt-DPC
completion, transaction release, and request-completion flow.

## USB

`usb_device`, `usb_interface`, and `usb_pipe` are non-owning typed facades over
KMDF USB objects. Create and configure the target in
`EvtDevicePrepareHardware`, start a configured continuous reader in
`EvtDeviceD0Entry`, and stop it in `EvtDeviceD0Exit`:

```cpp
auto usb = ntl::kmdf::usb_device::try_create(device);
if (!usb)
  return usb.status();

auto selection = ntl::kmdf::usb_select_config::single_interface();
auto status = usb->try_select(selection);
if (status.is_err())
  return status;

auto interface = selection.configured_interface();
ntl::kmdf::usb_pipe_information info;
auto pipe = interface.pipe_at(0, &info);
```

USB target creation, descriptor retrieval, configuration selection, and the
synchronous transfer helpers require `PASSIVE_LEVEL`. The asynchronous
formatting helpers only prepare a WDF request and are valid through
`DISPATCH_LEVEL`; send the formatted request through `usb_device::target()` or
`usb_pipe::target()`.

The continuous-reader callback may run at `DISPATCH_LEVEL`. It must use only
operations valid at the actual callback IRQL and must not call the general
PASSIVE_LEVEL CRT/STL surface. Defer such work to a KMDF work item or another
passive callback. The reader-failure callback runs at `PASSIVE_LEVEL`:

```cpp
constexpr auto on_packet =
    +[](ntl::kmdf::usb_pipe, ntl::kmdf::memory buffer,
        size_t transferred, void* context) noexcept {
      auto& count = *static_cast<std::atomic_uint32_t*>(context);
      count.fetch_add(1, std::memory_order_relaxed);
      // Do not perform PASSIVE_LEVEL-only STL/CRT work here.
    };

auto reader =
    ntl::kmdf::usb_continuous_reader_config::with_completion<on_packet>(
        packet_size, &packet_count);
status = input_pipe.try_configure_reader(reader);
```

Package builds instantiate the USB device, interface, pipe, synchronous and
formatted-transfer, and continuous-reader surfaces for every supported
toolset and architecture. Runtime USB validation requires a device whose
descriptor and endpoint protocol match the driver. The buildable
[KMDF USB driver template](../../examples/kmdf-usb-ntl-driver) deliberately
uses a placeholder hardware ID so it cannot be installed accidentally for an
unrelated USB device.

## Interrupts

`interrupt_config` installs compile-time callbacks without dynamic allocation.
`interrupt::try_create()` preserves `WdfInterruptCreate` status and can also
construct an NTL-managed C++ interrupt context.

| Callback | Typed signature | Execution level |
| --- | --- | --- |
| ISR | `bool(interrupt, ULONG) noexcept` | DIRQL, or `PASSIVE_LEVEL` for a passive interrupt |
| DPC | `void(interrupt, object) noexcept` | `DISPATCH_LEVEL` |
| Work item | `void(interrupt, object) noexcept` | `PASSIVE_LEVEL` |
| Enable/disable | `NTSTATUS(interrupt, device) noexcept` | Interrupt service level |

```cpp
constexpr auto on_interrupt =
    +[](kmdf::interrupt interrupt, ULONG) noexcept {
      // Inspect and acknowledge hardware here.
      return interrupt.queue_dpc();
    };
constexpr auto on_interrupt_dpc =
    +[](kmdf::interrupt interrupt, kmdf::object) noexcept {
      // DISPATCH_LEVEL: defer CRT/STL work if necessary.
      interrupt.queue_work_item();
    };
constexpr auto on_interrupt_work_item =
    +[](kmdf::interrupt, kmdf::object) noexcept {
      // PASSIVE_LEVEL work may use the audited CRT/STL surface.
    };

auto config =
    ntl::kmdf::interrupt_config::with_isr<on_interrupt>();
config
    .on_dpc<on_interrupt_dpc>()
    .on_work_item<on_interrupt_work_item>();

auto created = ntl::kmdf::interrupt::try_create(device, config);
if (!created)
  return created.status();
```

`interrupt_lock` pairs `WdfInterruptAcquireLock` and
`WdfInterruptReleaseLock`; `interrupt::synchronize()` wraps
`WdfInterruptSynchronize`. These helpers retain the native interrupt IRQL and
deadlock contracts. `info()`, `policy()`, and `extended_policy()` expose vector
and affinity configuration, while `enable()`, `disable()`, `report_active()`,
and `report_inactive()` preserve KMDF's explicit interrupt lifecycle.

## Timer And Work Item

`ntl::kmdf::timer` and `ntl::kmdf::work_item` are WDF objects parented to a
device, queue, or another WDF object. They are different from the WDM-oriented
`ntl::timer` and `ntl::work_item`: KMDF owns their lifetime and can serialize
their callbacks with the parent object's callbacks.

```cpp
constexpr auto on_work_item = +[](kmdf::work_item item) noexcept {
  auto& state = item.parent().context<device_state>();
  state.refresh_at_passive_level();
};
constexpr auto on_timer = +[](kmdf::timer timer) noexcept {
  timer.parent().context<device_state>().poll();
};

auto work_config =
    ntl::kmdf::work_item_config::with_callback<on_work_item>();
work_config.automatic_serialization(false);

auto work = ntl::kmdf::work_item::try_create(device, work_config);
if (!work)
  return work.status();
work->enqueue();

auto timer_config =
    ntl::kmdf::timer_config::periodic<on_timer>(1000);

ntl::kmdf::object_attributes timer_attributes;
timer_attributes.execution_level(WdfExecutionLevelPassive);
auto timer = ntl::kmdf::timer::try_create(
    device, timer_config, &timer_attributes);
if (!timer)
  return timer.status();
timer->start_after_ms(1000);
```

A work-item callback always runs at `PASSIVE_LEVEL`; `flush()` also requires
`PASSIVE_LEVEL` and must not be called from that work item's callback. A timer
normally follows WDF timer execution rules. Select
`WdfExecutionLevelPassive` when its body uses the CRT/STL; WDF requires such a
passive timer to be one-shot (`Period == 0`). `timer::stop(true)` waits for an
active callback and therefore requires `PASSIVE_LEVEL`. High-resolution timers
must keep `TolerableDelay` at zero, as required by WDF.

## Dynamic Child Lists

`child_list_config<Identification, Address>` maps KMDF child descriptions to
typed payloads. NTL deliberately requires trivially copyable, standard-layout
payloads because KMDF's default description behavior copies and compares their
bytes. Drivers that need allocated strings or another complex identity can
still use the native WDF callbacks through `child_list_config::native()`.

```cpp
struct child_id { ULONG serial; };
struct child_address { ULONG slot; };

using children = ntl::kmdf::child_list_config<child_id, child_address>;
constexpr auto on_child_create =
    +[](kmdf::child_list,
        const kmdf::child_identification<child_id>& id,
        kmdf::pdo_init init) noexcept -> NTSTATUS {
      auto status = init.assign_device_id(L"Sample\\Child");
      if (status.is_err())
        return status;
      status = init.assign_instance_id(L"7");
      if (status.is_err())
        return status;
      status = init.add_hardware_id(L"Sample\\Child");
      if (status.is_err())
        return status;

      auto child = init.try_create();
      return child ? STATUS_SUCCESS : child.status();
    };
auto config = children::with_create<on_child_create>();

// Configure the FDO's framework-owned default list before device creation.
init.default_child_list(config);

// Additional child lists can be created after the parent device exists.
auto list = ntl::kmdf::child_list::try_create(parent_device, config);
if (!list)
  return list.status();

const kmdf::child_identification<child_id> id{{7}};
const kmdf::child_address<child_address> address{{2}};
return list->add_or_update(id, address);
```

The facade also exposes scan begin/end, present/missing updates, typed PDO
lookup, address lookup, eject requests, and RAII iteration. `pdo_init` keeps
the ownership distinction explicit: the value supplied to a dynamic
child-create callback is framework-owned, while `pdo_init::try_allocate()`
owns a static PDO initializer until `try_create()` consumes it. Its typed
surface covers device, instance, hardware, compatible, and container IDs,
localized device text, raw-device assignment, and forwarding to the parent.

The resulting `pdo` facade supports parent lookup, identification/address
round trips, missing/eject requests, ejection relations, and PnP/power
capabilities. A statically allocated child can be attached with
`device::try_add_static_child()`. The buildable
[KMDF bus sample](../../examples/kmdf-bus-ntl-driver) exercises dynamic plug,
missing, and eject transitions against a real child function driver.

### PDO Events And Resource Requirements

`pdo_event_callbacks` covers the PDO-specific resource, eject, lock, wake, and
reported-missing events in `WDF_PDO_EVENT_CALLBACKS`. Register the table on
`pdo_init` before `try_create()`. WDF invokes these callbacks at
`PASSIVE_LEVEL`.

Resource reporting has three distinct stages:

- `on_resource_requirements_query()` receives `io_resource_requirements` and
  reports the logical configurations that PnP may arbitrate.
- `on_resources_query()` receives a mutable `resource_list` for firmware or
  boot resources already assigned to the child.
- `pnp_power_callbacks::on_prepare_hardware()` receives the final raw and
  translated `resource_list` values selected by PnP.

`io_resource_descriptor` owns an `IO_RESOURCE_DESCRIPTOR` and supplies
factories for memory, port, interrupt, legacy and v3 DMA, connection, and
device-private requirements. `io_resource_list` represents one logical
configuration; `io_resource_requirements` contains one or more alternatives.
The facades copy descriptors into WDF lists and retain `native()` or
`native_handle()` access for bus-specific fields.

```cpp
constexpr auto query_requirements =
    +[](kmdf::pdo child,
        kmdf::io_resource_requirements requirements) noexcept -> NTSTATUS {
  requirements.interface_type(Internal).slot_number(0);

  auto configuration = requirements.try_create_list();
  if (!configuration)
    return configuration.status();

  PHYSICAL_ADDRESS minimum{};
  PHYSICAL_ADDRESS maximum{};
  maximum.QuadPart = 0xffff;
  auto status = configuration->try_append(
      kmdf::io_resource_descriptor::port(
          minimum, maximum, 8, 1, CM_RESOURCE_PORT_IO));
  if (status.is_err())
    return status;
  return requirements.try_append(configuration.value());
};

constexpr auto query_boot_resources =
    +[](kmdf::pdo, kmdf::resource_list resources) noexcept -> NTSTATUS {
  // A software-enumerated child can legitimately have no boot resources.
  return resources.size() == 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
};

ntl::kmdf::pdo_event_callbacks events;
events.on_resource_requirements_query<query_requirements>()
    .on_resources_query<query_boot_resources>();
init.events(events);
```

Do not invent memory ranges, interrupts, or DMA channels merely to populate a
test list. A virtual child that requires no hardware resources should append
an empty logical configuration, as the bus sample does. Hardware bus drivers
should report only ranges and alternatives that their bus contract actually
supports.

### Driver-Defined Query Interfaces

A provider starts with the normal Windows `INTERFACE` header and adds typed
operations. `make_query_interface()` initializes the common header, while
`query_interface_config` registers the contract on a WDF device:

```cpp
struct counter_interface {
  INTERFACE header;
  NTSTATUS(NTAPI *next)(void* context, ULONG* value) noexcept;
};

auto interface = ntl::kmdf::make_query_interface<counter_interface>(
    1, child.native_handle(),
    ntl::kmdf::reference_query_interface_object,
    ntl::kmdf::dereference_query_interface_object);
interface.next = next_counter;

ntl::kmdf::query_interface_config config{interface, counter_interface_guid};
auto status = child.try_add_query_interface(config);
```

The function driver queries the interface through its device stack. The
returned `queried_interface<T>` is move-only and calls
`InterfaceDereference` exactly once when reset or destroyed:

```cpp
auto queried = device.try_query_interface<counter_interface>(
    counter_interface_guid, 1);
if (!queried)
  return queried.status();

auto interface = std::move(queried).value();
ULONG value = 0;
auto status = interface->next(interface->header.Context, &value);
```

The GUID and structure layout form a cross-driver ABI. Keep the first member
as `INTERFACE`, use fixed-width payload types, and increment the interface
version when the contract changes. These helpers do not hide WDF ownership:
the provider chooses the reference callbacks and the consumer owns the
acquired reference through `queried_interface<T>`.

## Registry And Device Properties

`device::try_open_registry_key()` returns a move-only
`ntl::kmdf::registry_key`. It provides relative subkey open/create, raw value
query/assignment, DWORD/QWORD/string/multi-string helpers, and removal while
retaining the native `WDFKEY` and WDM handle escape hatches.

```cpp
auto key = device.try_open_registry_key(
    PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE);
if (!key)
  return key.status();

auto enabled = key->query_dword(L"Enabled");
if (!enabled)
  return enabled.status();

auto status = key->assign_string(L"Mode", L"safe");
if (status.is_err())
  return status;

status = key->assign_multi_string(L"Fallbacks", {L"primary", L"safe"});
if (status.is_err())
  return status;

auto description =
    device.try_query_property(DevicePropertyDeviceDescription);
```

Legacy `DEVICE_REGISTRY_PROPERTY` queries return raw bytes. The `DEVPROPKEY`
overload returns `device_property_value`, including its `DEVPROPTYPE`, with
checked `as_string()` and `as_uint32()` conversions. These NTL helpers require
`PASSIVE_LEVEL` because they return allocated STL storage. The underlying
native `WdfDeviceQueryPropertyEx` API remains available when a driver needs
its wider `APC_LEVEL` contract and supplies its own storage.

## WMI Providers And Instances

`wmi_provider` and `wmi_instance` are non-owning facades for KMDF's
framework-owned WMI objects. A provider belongs to a PnP device and cannot be
created for a control device. `wmi_provider_config` declares one data-block
GUID; `wmi_instance_config` connects typed query, set, set-item, and method
callbacks to an instance of that provider.

```cpp
struct telemetry_state {
  std::uint32_t value = 7;
};

constexpr auto query_telemetry =
    +[](ntl::kmdf::wmi_instance instance,
        ntl::kmdf::wmi_output_buffer output) noexcept -> NTSTATUS {
  return output.try_write(instance.context<telemetry_state>());
};

ntl::kmdf::wmi_provider_config provider(telemetry_guid);
provider.minimum_instance_buffer_size(sizeof(telemetry_state));

auto created_provider =
    ntl::kmdf::wmi_provider::try_create(device, provider);
if (!created_provider)
  return created_provider.status();

ntl::kmdf::wmi_instance_config instance(created_provider.value());
instance.register_automatically().on_query<query_telemetry>();

auto telemetry = ntl::kmdf::wmi_instance::try_create<telemetry_state>(
    device, instance, nullptr);
```

Assign the compiled MOF resource name before the device starts:

```cpp
auto status = device.try_assign_mof_resource(L"DriverMofResource");
```

`wmi_input_buffer`, `wmi_output_buffer`, and `wmi_method_buffer` validate
fixed-size trivially-copyable payloads and preserve WMI's required-size
reporting. `use_native_context_for_query()` is the native WDF shortcut and
must not be used with an NTL-managed C++ context, because the managed context
also contains construction metadata.

Automatic registration occurs on the first D0 entry. Manual instances can use
`try_register()` and `deregister()` at `PASSIVE_LEVEL`. Event-only providers
are created through the `wmi_instance_config(wmi_provider_config&)`
single-instance path and use `try_fire_event()` at IRQL no higher than
`APC_LEVEL`. Query, set, set-item, method, and provider function-control
callbacks run at `PASSIVE_LEVEL`; the function-control callback is ignored
for event-only providers. WDF does not permit an explicit
`ExecutionLevel` on WMI provider or instance objects, so their
`object_attributes` must retain `WdfExecutionLevelInheritFromParent`. Set a
passive contract on supported parent objects such as the device or queue, and
always follow each native KMDF WMI callback's documented IRQL contract.

The buildable [KMDF WMI sample](../../examples/kmdf-wmi-ntl-driver) compiles
and validates a binary MOF resource, runs typed query/set/method callbacks,
subscribes to an event from user mode, triggers it through a device interface,
and verifies the event payload through `ROOT\\WMI`.

## Queue Control

`io_queue` exposes start, stop, drain, purge, and stop-and-purge operations.
The `*_and_wait()` forms call the synchronous WDF methods and therefore require
the same `PASSIVE_LEVEL` and deadlock precautions as the native APIs. The
asynchronous forms optionally take a compile-time
`void(io_queue, void*) noexcept` callback and an opaque context pointer.

`native()` and `native_handle()` are explicit interoperation escape hatches.
File and interrupt objects use `native_object()` to reflect object semantics.
They do not imply that normal NTL callbacks should expose `WDFDEVICE`,
`WDFQUEUE`, or `WDFREQUEST`; the typed callback surface has the same WDF object
lifetime and synchronization rules without adding dynamic dispatch or storage.

## FDO, Control, And Object Utilities

`fdo_event_callbacks` covers the framework's FDO resource-filter stage with
typed `device`, `io_resource_requirements`, and `resource_list` arguments.
The callbacks run at `PASSIVE_LEVEL` and are installed before device creation:

```cpp
ntl::kmdf::fdo_event_callbacks events;
events.on_add_resource_requirements<filter_requirements>()
      .on_remove_resource_requirements<remove_requirements>()
      .on_remove_added_resources<remove_added_resources>();
init.fdo_events(events);
```

`device::try_name()` and `try_interface_string()` return framework-owned
`ntl::kmdf::string` objects. A control device can register a typed shutdown
notification through `control_device_init::on_shutdown()`. Use
`object_lock_guard` only for a WDF object created with a non-none
synchronization scope; the guard balances `WdfObjectAcquireLock` and
`WdfObjectReleaseLock` without allocating a second lock.

## KMDF Surface Boundary

The NTL surface covers the common control, PnP, filter, function, and bus
driver paths: entry and device creation, typed contexts and callbacks, queues,
forward progress and cancellation, requests and targets, files, PnP/power,
resources, interrupts, timers/work items/DPCs, child lists/PDOs, query
interfaces, FDO resource filtering, registry/properties, DMA, USB, WMI,
control-device shutdown, and framework object synchronization.

NTL deliberately does not rename every WDF function. Raw IRP dispatch and
preprocessing, miniport integration, verifier bugcheck helpers, device-family
protocol structures, and uncommon version-specific fields remain available
through `native()`, `native_handle()`, `native_object()`, `wdm_*()`, and the
normal WDK headers. Those paths are explicit framework interoperation, not a
missing second implementation of WDF.

## Execution Context

The entry function and the sample queue use `PASSIVE_LEVEL` execution. A WDF
callback may run at a higher IRQL when its queue/object configuration allows
that, so using CRT/STL inside a callback still requires a passive execution
contract such as `object_attributes::execution_level(WdfExecutionLevelPassive)`.
All native KMDF callback, cancellation, synchronization, and lifetime rules
remain in force.
