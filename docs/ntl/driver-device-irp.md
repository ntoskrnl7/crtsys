# NTL Driver, Device, and IRP Helpers

[Back to NTL docs](./README.md)

This page covers the driver-facing helper classes used to wire up a normal WDK
driver with C++ callbacks.

## Entry Point

When `CRTSYS_NTL_MAIN` is enabled, implement:

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys` routes the WDK driver entry point into this function. The wrapper also
uses the stack expansion helper before calling into `ntl::main`.

Example:

```cpp
#include <ntl/driver>
#include <ntl/registry>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  ULONG flags = 0;

  auto parameters = ntl::try_open_driver_parameters(registry_path);
  if (parameters) {
    auto configured_flags = parameters->query_dword(L"Flags");
    if (configured_flags) {
      flags = *configured_flags;
    }
  } else if (static_cast<NTSTATUS>(parameters.status()) !=
             STATUS_OBJECT_NAME_NOT_FOUND) {
    return parameters.status();
  }

  (void)flags;

  driver.on_unload([] {
    // Release driver-owned objects here.
  });

  return ntl::status::ok();
}
```

IRQL: `PASSIVE_LEVEL`.

`registry_path` is the service key path supplied by the I/O manager. Use
[`ntl::try_open_driver_parameters`](./registry.md) when the driver has optional
configuration values below the standard `Parameters` subkey.

## Driver Object

Header: [`include/ntl/driver`](../../include/ntl/driver)

`ntl::driver` wraps `DRIVER_OBJECT`.

API:

- `create_device<Extension>(device_options&)`
  - creates an `ntl::device<Extension>`
  - constructs the extension object in the device extension area
- `try_create_device<Extension>(device_options&)`
  - creates an `ntl::device<Extension>`
  - returns `ntl::result<std::shared_ptr<ntl::device<Extension>>>` with the
    `IoCreateDevice` status instead of throwing for creation failure
- `on_unload(callback)`
  - registers a C++ unload callback
- `name() const`
  - returns the driver name as `std::wstring`

Example:

```cpp
struct device_extension {
  ULONG open_count = 0;
};

ntl::device_options options;
options.name(L"demo").type(FILE_DEVICE_UNKNOWN);

auto device = driver.create_device<device_extension>(options);
device.extension().open_count = 0;

driver.on_unload([device = std::move(device)]() mutable {
  device.detach();
});
```

Use `try_create_device` when the initialization path should preserve
`NTSTATUS` without converting `IoCreateDevice` failure into an exception:

```cpp
auto device = driver.try_create_device<device_extension>(options);
if (!device) {
  return device.status();
}

(*device)->extension().open_count = 0;
```

IRQL: `PASSIVE_LEVEL`. The helper uses C++ objects and containers and is
intended for driver initialization, unload registration, and setup paths.

## Device Endpoint

Header: [`include/ntl/device_endpoint`](../../include/ntl/device_endpoint)

`ntl::device_endpoint<Extension>` is a copyable owning handle for an
`ntl::device<Extension>` and the DOS-device symbolic link that exposes it.
Copies share one endpoint state. Closing any copy closes that state for every
copy, and the operation is idempotent. The link is always deleted before the
device object is released.

Use it when a driver wants the common pair:

- `\\Device\\name`
- `\\DosDevices\\name`

Example:

```cpp
#include <ntl/device_endpoint>

struct device_extension {
  ULONG open_count = 0;
};

ntl::device_options options;
options.name(L"demo").type(FILE_DEVICE_UNKNOWN);

auto endpoint_result = ntl::try_create_device_endpoint<device_extension>(
    driver, options);
if (!endpoint_result) {
  return endpoint_result.status();
}

auto endpoint = std::move(*endpoint_result);
auto device = endpoint.device();
if (!device)
  return STATUS_INVALID_DEVICE_STATE;
device->extension().open_count = 0;

driver.on_unload([endpoint]() noexcept {
  const ntl::status result = endpoint.close();
  NT_ASSERT(result.is_ok());
});
```

API:

- `try_create_device_endpoint<Extension>(driver, options)`
  - creates the device through `driver.try_create_device`
  - creates `\\DosDevices\\` + `options.name()` targeting
    `\\Device\\` + `options.name()`
  - returns `ntl::result<ntl::device_endpoint<Extension>>`
- `try_create_device_endpoint<Extension>(driver, options, link_name)`
  - creates the device through `driver.try_create_device`
  - creates `link_name` targeting `\\Device\\` + `options.name()`
  - returns `ntl::result<ntl::device_endpoint<Extension>>`
- `create_device_endpoint<Extension>(driver, options)`
  - throws `ntl::exception` on creation failure
- `create_device_endpoint<Extension>(driver, options, link_name)`
  - throws `ntl::exception` on creation failure
- `dos_device_name(short_name)`
- `device_target_name(short_name)`
- `device_endpoint<Extension>::device()`
  - returns the shared `ntl::device<Extension>` owner
- `device_endpoint<Extension>::unpublish()`
  - rejects new user-mode opens while retaining the device object for a
    composed drain path
- `device_endpoint<Extension>::close()`
  - idempotently deletes the link and releases the device for all copies
- `device_endpoint<Extension>::link_name()`
- `device_endpoint<Extension>::target_name()`
- `device_endpoint<Extension>::valid()` / `operator bool()`

`device_options::name()` is the short device name without the `\\Device\\`
prefix. The endpoint factory builds the native target path from that name. The
two-argument endpoint factory also builds the usual DOS link name from that
same short name, so the common case only needs the device name once.
Capturing an endpoint by value retains its shared owning state. Callers do not
need to wrap it in another `std::shared_ptr`. A subsystem that must reject new
opens before draining external work calls `unpublish()`, drains that work, and
then calls `close()`. Put that sequence in the subsystem's owning runtime so
ordinary callers invoke only one runtime `close()` operation.

An owning device returned by `device()` may outlive the endpoint facade. After
`close()`, every endpoint copy reports closed and returns no new device owner,
while an already-retained device owner remains valid until that owner is
released. This prevents facade destruction order from invalidating a child
that is still in use.

Creation, accessors, `unpublish()`, and `close()` require `PASSIVE_LEVEL`.
Destroying the last endpoint handle is safe through `DISPATCH_LEVEL`: NTL
defers the endpoint state's final cleanup to its joined `PASSIVE_LEVEL`
runtime worker when necessary. The caller does not queue or drain a work item.

### Typed IOCTL routing

`on_ioctl<Contract>()` is not a callback with an inferred or hidden request
layout. `Contract::input_type` and `Contract::output_type` define the exact
shared app/driver wire structures, and the contract also owns the `CTL_CODE`
fields. For example:

```cpp
struct configure_proxy_request {
  std::uint16_t port = 0;
};

struct configure_proxy_reply {
  std::uint32_t generation = 0;
};

struct configure_proxy_contract {
  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x900;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
  using input_type = configure_proxy_request;
  using output_type = configure_proxy_reply;
};

const ntl::status routed =
    endpoint.on_ioctl<configure_proxy_contract>(
        [](const configure_proxy_request &request,
           configure_proxy_reply &reply) noexcept -> ntl::status {
          if (request.port == 0)
            return STATUS_INVALID_PARAMETER;
          reply.generation = static_cast<std::uint32_t>(request.port);
          return ntl::status::ok();
        });
if (!routed.is_ok())
  return routed;
```

The router derives the callback signature from those two types. A `void`
input removes the input parameter, a `void` output removes the output
parameter, and a non-`void` endpoint extension is passed as the first
parameter. Payload types must be trivially copyable. The owning typed route is
restricted to `METHOD_BUFFERED`, validates the exact input size, initializes
and reports the exact output size, owns the callback capture, rejects duplicate
codes, and drains callbacks during endpoint close. Direct-I/O,
`METHOD_NEITHER`, or deliberately pending IRPs use the explicitly low-level
`on_borrowed_pending_ioctl()` path instead.

## IRP View

Header: [`include/ntl/irp`](../../include/ntl/irp)

`ntl::irp` is a non-owning view over the dispatch-time `PIRP`. It does not
complete, reference, or retain the IRP.

API:

- `get() const`
- `operator->() const`
- `stack_location() const`
- `major_function() const`
- `status() const` / `status(NTSTATUS)`
- `information() const` / `information(ULONG_PTR)`
- `set_result(NTSTATUS, ULONG_PTR = 0)`
- `succeed(ULONG_PTR = 0)`
- `fail(NTSTATUS)`

Example:

```cpp
device.on_create([](ntl::irp& request) {
  request.succeed();
});
```

`set_result`, `succeed`, and `fail` set `IoStatus.Status` and
`IoStatus.Information`. They do not call `IoCompleteRequest`; the NTL dispatch
invoker completes the IRP after the callback returns.

IRQL: follows the dispatch routine that supplied the IRP.

## Device Object

Header: [`include/ntl/device`](../../include/ntl/device)

`ntl::device_options` configures device creation.

API:

- `name(std::wstring)`
- `type(DEVICE_TYPE)`
- `exclusive(bool = true)`
- `name() const`
- `type() const`
- `is_exclusive() const`

`ntl::device<Extension>` owns a `PDEVICE_OBJECT`.

API:

- `extension()`
- `on_create(callback)`
- `on_close(callback)`
- `on_device_control(callback)`
- `name() const`
- `type() const`
- `detach()`

Device control helper types:

- `ntl::device_control::code`
- `ntl::device_control::in_buffer`
- `ntl::device_control::out_buffer`
- `ntl::device_control::dispatch_fn`

`in_buffer` provides `can_read(bytes)` and `as<T>()` for
trivially-copyable request payloads. `out_buffer` provides
`can_write(bytes)`, `clear()`, `as<T>()`, `write_bytes(ptr, bytes)`, and
`write(value)` for reporting an exact output byte count through
`IoStatus.Information`.

For IOCTLs with fixed trivially-copyable request and reply payloads, use
[`ntl::ioctl`](./ioctl.md) to tie the `CTL_CODE` value to those payload types.
That keeps the raw IOCTL number visible while reducing repeated size checks in
dispatch code.

For a complete dispatch-body pattern that combines typed IOCTLs,
`ntl::remove_lock`, `ntl::mdl`, and output byte-count reporting, see
[`Device-control pattern`](./device-control-pattern.md).

Example:

```cpp
struct demo_reply {
  ULONG value;
};

device.on_device_control([](const ntl::device_control::code& code,
                            const ntl::device_control::in_buffer& in,
                            ntl::device_control::out_buffer& out) {
  if (code != DEMO_IOCTL_PING) {
    out.clear();
    return;
  }

  const auto* request = in.as<ULONG>();
  if (!request) {
    out.clear();
    return;
  }

  demo_reply reply{*request + 1};
  if (!out.write(reply)) {
    out.clear();
  }
});
```

IRQL: `PASSIVE_LEVEL` unless a specific dispatch path has been audited and
documented otherwise. The wrapper uses C++ callbacks and ownership helpers.

## Symbolic Link

Header: [`include/ntl/symbolic_link`](../../include/ntl/symbolic_link)

`ntl::symbolic_link` owns a WDK symbolic link created by
`IoCreateSymbolicLink` and deletes it with `IoDeleteSymbolicLink`.

Example:

```cpp
ntl::symbolic_link link(L"\\DosDevices\\demo", L"\\Device\\demo");
```

Move it into the unload callback together with the device object when the link
should live for the driver lifetime.

IRQL: `PASSIVE_LEVEL`.
