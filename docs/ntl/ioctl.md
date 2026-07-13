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

struct ping_contract {
  using input_type = ping_request;
  using output_type = ping_reply;

  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x801;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
};

using ioctl_ping = ntl::ioctl_from_contract<ping_contract>;

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
- `ntl::ioctl_from_contract<Contract>`
- `Ioctl::code`
- `Ioctl::control_code()`
- `ntl::is_ioctl<Ioctl>(code)`
- `ntl::ioctl_input_as<Ioctl>(in_buffer)`
- `ntl::ioctl_write_output<Ioctl>(out_buffer, value)`

The input and output helper functions require trivially copyable payload types.
Use `void` for the input or output type when an IOCTL has no typed payload in
that direction.

`ioctl_from_contract` is intended for shared app/driver contract headers. Keep
the raw `CTL_CODE` fields and payload types in one contract type, then let the
driver derive the NTL descriptor from that type.

## IRQL

The helper itself is only type and buffer arithmetic. It follows the dispatch
path that supplied the IRP. The callback body still controls the real IRQL
contract.

## Driver Test Coverage

The driver test suite exercises:

- compile-time `CTL_CODE` agreement
- shared contract to typed descriptor mapping
- runtime code matching and mismatch
- typed input view
- typed output write
- short output buffer rejection
