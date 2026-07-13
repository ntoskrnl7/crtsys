# NTL Device Interface

[Back to NTL docs](./README.md)

Header: [`include/ntl/device_interface`](../../include/ntl/device_interface)

`ntl::device_interface_link` owns the symbolic link returned by
`IoRegisterDeviceInterface`. This is for PnP device interfaces, not for a named
legacy control device. For a `\\Device\\name` plus `\\DosDevices\\name` pair,
use [`ntl::device_endpoint`](./driver-device-irp.md#device-endpoint) instead.

## Example

```cpp
auto iface = ntl::try_register_device_interface(
    physical_device_object,
    GUID_DEVINTERFACE_DEMO);
if (!iface) {
  return iface.status();
}

auto status = iface->enable();
if (!status) {
  return status;
}
```

The `physical_device_object` must be a PDO accepted by
`IoRegisterDeviceInterface`. Passing an arbitrary control-device object is not
valid PnP device-interface usage.

## API Summary

- `ntl::try_register_device_interface(pdo, guid, reference_string)`
- `device_interface_link::enable()`
- `device_interface_link::disable()`
- `device_interface_link::set_enabled(bool)`
- `device_interface_link::native_name()`
- `device_interface_link::name()`
- `device_interface_link::release()`
- `device_interface_link::reset()`

`device_interface_link` disables an enabled interface and frees the native
symbolic-link string in its destructor.

## IRQL

Treat registration and state changes as `PASSIVE_LEVEL` PnP/control-path work.

## Driver Test Coverage

The generic driver test suite does not fabricate a fake PDO. It covers the empty
owner safety path. Real registration is expected to be tested by a PnP driver
that owns a valid physical device object.
