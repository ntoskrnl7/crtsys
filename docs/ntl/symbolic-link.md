# NTL Symbolic Link Helper

[Back to NTL docs](./README.md)

`ntl::symbolic_link` is a small RAII wrapper over the WDK
`IoCreateSymbolicLink` / `IoDeleteSymbolicLink` pair.

It is intended for driver initialization and teardown paths where a driver
exposes a named device through a DOS-device style symbolic link.

Header: [`include/ntl/symbolic_link`](../../include/ntl/symbolic_link)

## Example

```cpp
#include <ntl/driver>
#include <ntl/symbolic_link>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  ntl::device_options options;
  options.name(L"\\Device\\demo").type(FILE_DEVICE_UNKNOWN);
  auto device = driver.create_device<void>(options);

  ntl::symbolic_link link(L"\\DosDevices\\demo", L"\\Device\\demo");

  driver.on_unload([device = std::move(device),
                    link = std::move(link)]() mutable {
    link.reset();
    device.detach();
  });

  return ntl::status::ok();
}
```

## API

- `symbolic_link(std::wstring link_name, std::wstring target_name)`
  - creates the symbolic link
  - throws `ntl::exception` if `IoCreateSymbolicLink` fails
- `create(link_name, target_name)`
  - deletes any currently owned link, then creates a new one
  - returns `ntl::status`
- `close()`
  - deletes the owned link and reports the `IoDeleteSymbolicLink` status
- `reset()`
  - deletes the owned link and ignores teardown failure
- `release()`
  - detaches ownership without deleting the symbolic link
- `name()`
- `target_name()`
- `valid()` / `operator bool()`

## IRQL

Use from `PASSIVE_LEVEL` driver setup and teardown code. The helper owns
`std::wstring` state and calls WDK object-manager routines, so it is not a DPC
or ISR helper.

## Notes

The wrapper does not hide the WDK naming model. Pass the native names you would
pass to `IoCreateSymbolicLink`, such as `\\DosDevices\\name` for the link and
`\\Device\\name` for the target.
