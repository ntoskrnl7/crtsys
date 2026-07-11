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

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  driver.on_unload([] {
    // Release driver-owned objects here.
  });

  return ntl::status::ok();
}
```

IRQL: `PASSIVE_LEVEL`.

## Driver Object

Header: [`include/ntl/driver`](../../include/ntl/driver)

`ntl::driver` wraps `DRIVER_OBJECT`.

API:

- `create_device<Extension>(device_options&)`
  - creates an `ntl::device<Extension>`
  - constructs the extension object in the device extension area
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
options.name(L"\\Device\\demo").type(FILE_DEVICE_UNKNOWN);

auto device = driver.create_device<device_extension>(options);
device.extension().open_count = 0;

driver.on_unload([device = std::move(device)]() mutable {
  device.detach();
});
```

IRQL: `PASSIVE_LEVEL`. The helper uses C++ objects and containers and is
intended for driver initialization, unload registration, and setup paths.

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

Example:

```cpp
device.on_create([](ntl::irp& request) {
  request.status(STATUS_SUCCESS);
  request.information(0);
});
```

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

Example:

```cpp
device.on_device_control([](ntl::irp& request) {
  auto* stack = request.stack_location();
  const auto code = stack->Parameters.DeviceIoControl.IoControlCode;

  if (code != DEMO_IOCTL_PING) {
    request.status(STATUS_INVALID_DEVICE_REQUEST);
    request.information(0);
    return;
  }

  request.status(STATUS_SUCCESS);
  request.information(0);
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
