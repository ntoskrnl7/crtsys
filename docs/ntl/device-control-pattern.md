# NTL Device-Control Pattern

[Back to NTL docs](./README.md)

This page shows how the lower-level NTL device-control helpers fit together in
a realistic dispatch body. The goal is not to hide the WDK model: the IOCTL
number, input/output byte counts, remove-lock lifetime, and MDL mapping rules
remain visible.

## Pattern

Use this shape for fixed-size buffered IOCTLs:

1. Acquire an `ntl::remove_lock` guard for the dispatch path.
2. Match the runtime `ntl::device_control::code` against a typed
   `ntl::ioctl` descriptor.
3. Read the request with `ntl::ioctl_input_as`.
4. Prepare or map any kernel buffer with `ntl::mdl` when the code needs an MDL
   owner.
5. Write the reply with `ntl::ioctl_write_output`.
6. Return an `NTSTATUS` and leave `out_buffer::size` as the exact
   `IoStatus.Information` byte count.

```cpp
struct ping_request {
  ULONG value;
};

struct ping_reply {
  ULONG value;
  ULONG checksum;
};

using ioctl_ping =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, ping_request, ping_reply>;

class device_state {
public:
  NTSTATUS on_device_control(const ntl::device_control::code& code,
                             const ntl::device_control::in_buffer& in,
                             ntl::device_control::out_buffer& out,
                             PIRP irp) noexcept {
    auto guard = remove_lock_.acquire(irp);
    if (!guard) {
      out.clear();
      return static_cast<NTSTATUS>(guard.status());
    }

    if (!ntl::is_ioctl<ioctl_ping>(code)) {
      out.clear();
      return STATUS_INVALID_DEVICE_REQUEST;
    }

    const auto* request = ntl::ioctl_input_as<ioctl_ping>(in);
    if (!request) {
      out.clear();
      return STATUS_INVALID_PARAMETER;
    }

    ping_reply reply{request->value + 1, request->value ^ 0xa5a5a5a5u};
    if (!ntl::ioctl_write_output<ioctl_ping>(out, reply)) {
      out.clear();
      return STATUS_BUFFER_TOO_SMALL;
    }

    return STATUS_SUCCESS;
  }

  void unload() noexcept {
    remove_lock_.release_and_wait(this);
  }

private:
  ntl::remove_lock remove_lock_{ntl::pool_tag("NTPp")};
};
```

In an `ntl::device` callback, capture the device state and call the handler:

```cpp
device.on_device_control([state](const ntl::device_control::code& code,
                                 const ntl::device_control::in_buffer& in,
                                 ntl::device_control::out_buffer& out) {
  const NTSTATUS status = state->on_device_control(code, in, out, nullptr);
  if (!NT_SUCCESS(status)) {
    // Throwing ntl::exception lets the NTL dispatch invoker copy the status
    // into IRP->IoStatus.Status and complete the IRP.
    throw ntl::exception(status, "device-control request failed");
  }
});
```

For hot or pageable paths, keep the native WDK rules in charge. `ntl::mdl`
owns `IoAllocateMdl` storage and unmaps/unlocks on reset/destruction, but the
caller still chooses whether the buffer is nonpaged, probed-and-locked, or
mapped with `MmGetSystemAddressForMdlSafe`.

## Tested Coverage

The driver test suite covers this pattern as
`ntl_device_control_pipeline_test`: success, unknown IOCTL, short input, short
output, MDL-backed scratch storage, and post-remove-lock teardown rejection.
