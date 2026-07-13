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

`ntl::device_endpoint<Extension>` owns both an `ntl::device<Extension>` and the
DOS-device symbolic link that exposes it. It deletes the link before releasing
the device object, which is the usual unload order for a named control device.

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

auto endpoint = ntl::try_create_device_endpoint<device_extension>(
    driver, options, L"\\DosDevices\\demo");
if (!endpoint) {
  return endpoint.status();
}

endpoint->device_object().extension().open_count = 0;

auto endpoint_owner =
    std::make_shared<ntl::device_endpoint<device_extension>>(std::move(*endpoint));
driver.on_unload([endpoint_owner] {
  endpoint_owner->reset();
});
```

API:

- `try_create_device_endpoint<Extension>(driver, options, link_name)`
  - creates the device through `driver.try_create_device`
  - creates `link_name` targeting `\\Device\\` + `options.name()`
  - returns `ntl::result<ntl::device_endpoint<Extension>>`
- `create_device_endpoint<Extension>(driver, options, link_name)`
  - throws `ntl::exception` on creation failure
- `device_endpoint<Extension>::device_owner()`
  - returns the shared `ntl::device<Extension>` owner
- `device_endpoint<Extension>::device_object()`
  - returns the referenced device wrapper
- `device_endpoint<Extension>::link()`
- `device_endpoint<Extension>::reset()`
- `device_endpoint<Extension>::link_name()`
- `device_endpoint<Extension>::target_name()`
- `device_endpoint<Extension>::valid()` / `operator bool()`

`device_options::name()` is the short device name without the `\\Device\\`
prefix. The endpoint factory builds the native target path from that name.
Because `driver.on_unload()` stores a `std::function<void()>`, move-only
endpoint owners should be held through `std::shared_ptr` when captured by the
unload callback.

IRQL: `PASSIVE_LEVEL`.

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
