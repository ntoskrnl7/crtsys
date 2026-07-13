# NTL Typed IOCTL Helper

[Back to NTL docs](./README.md)

Header: [`include/ntl/ioctl`](../../include/ntl/ioctl)

`ntl::ioctl` is a small compile-time descriptor for `CTL_CODE` values. It keeps
the native IOCTL number visible while tying that number to the request and reply
payload types used by a device-control handler.

## Example

```cpp
struct ping_request {
  ULONG value;
};

struct ping_reply {
  ULONG value;
};

using ioctl_ping =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA,
               ping_request, ping_reply>;

device.on_device_control([](const ntl::device_control::code& code,
                            const ntl::device_control::in_buffer& in,
                            ntl::device_control::out_buffer& out) {
  if (!ntl::is_ioctl<ioctl_ping>(code)) {
    out.clear();
    return;
  }

  const auto* request = ntl::ioctl_input_as<ioctl_ping>(in);
  if (!request) {
    out.clear();
    return;
  }

  const ping_reply reply{request->value + 1};
  if (!ntl::ioctl_write_output<ioctl_ping>(out, reply)) {
    out.clear();
  }
});
```

## API Summary

- `ntl::ioctl<DeviceType, Function, Method, Access, Input, Output>`
- `Ioctl::code`
- `Ioctl::device_control_code()`
- `ntl::is_ioctl<Ioctl>(code)`
- `ntl::ioctl_input_as<Ioctl>(in_buffer)`
- `ntl::ioctl_write_output<Ioctl>(out_buffer, value)`

The input and output helper functions require trivially copyable payload types.
Use `void` for the input or output type when an IOCTL has no typed payload in
that direction.

## IRQL

The helper itself is only type and buffer arithmetic. It follows the dispatch
path that supplied the IRP. The callback body still controls the real IRQL
contract.

## Driver Test Coverage

The driver test suite exercises:

- compile-time `CTL_CODE` agreement
- runtime code matching and mismatch
- typed input view
- typed output write
- short output buffer rejection
