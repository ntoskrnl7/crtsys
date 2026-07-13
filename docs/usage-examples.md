# NTL Usage Examples

[Back to README](../README.md)

This page shows small end-to-end shapes for using NTL from a driver and from a
companion user-mode application. The snippets are intentionally small; they are
meant to show ownership and build boundaries, not to replace normal WDK design
review.

For tested source, see:

- complete NTL sample driver: [`examples/ntl-driver`](../examples/ntl-driver)
- complete NTL RPC sample driver:
  [`examples/ntl-rpc-driver`](../examples/ntl-rpc-driver)
- shared RPC schema: [`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp)
- driver side: [`test/cmake/driver/src/main.cpp`](../test/cmake/driver/src/main.cpp)
- app side: [`test/cmake/app/src/main.cpp`](../test/cmake/app/src/main.cpp)
- NuGet consumer projects: [`test/nuget`](../test/nuget)

The sample driver is the best starting point when you want one compact project
that shows `ntl::main`, registry `Parameters`, device endpoint ownership,
typed IOCTLs, remove locks, passive work, and pool-backed PMR together.
Both `examples/ntl-driver` and `examples/ntl-rpc-driver` include Visual Studio
solutions that consume `crtsys` through NuGet, plus CMake projects for source
tree builds.

## Build Shape

Use `crtsys` in two different places:

- The kernel driver owns the device, RPC server, IOCTL callbacks, lifetime, and
  unload cleanup.
- The user-mode app opens the driver-created device and sends requests through
  `DeviceIoControl`, either directly or through `ntl::rpc::client`.

The NuGet package follows the same split:

- App projects use header-only app mode. This is enough for `ntl/rpc/client`.
- WDK driver projects use driver mode. This adds the driver libraries,
  entry-point wiring, include ordering, and WDK-compatible settings.

## Minimal Driver Entry

When `CRTSYS_NTL_MAIN` is enabled, implement `ntl::main` instead of writing
`DriverEntry` directly.

```cpp
#include <string>
#include <ntl/driver>
#include <ntl/registry>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  unsigned long flags = 0;
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

`ntl::main`, `ntl::driver`, and the helper callbacks are intended for driver
control paths. Treat them as `PASSIVE_LEVEL` unless an API documents a broader
contract.

## RPC Skeleton

The RPC helper generates a matching kernel server and user-mode client from one
shared schema header. The driver includes `<ntl/rpc/server>` before the schema;
the app includes `<ntl/rpc/client>` before the same schema.

For a buildable driver/app pair, see
[`examples/ntl-rpc-driver`](../examples/ntl-rpc-driver).

The compact `NTL_ADD_CALLBACK_N` macros use `__LINE__` as the callback ID. This
keeps small internal or test schemas easy to write, but it also means the schema
line number becomes part of the app/driver ABI. Both sides must include the
exact same schema file without generating different line numbers.

For a longer-lived ABI, use `NTL_ADD_CALLBACK_ID_N` and assign stable IDs
explicitly. Keep IDs unique within the schema and within the `CTL_CODE` function
field range.

### Shared Schema

```cpp
// shared/demo_rpc.hpp
#pragma once

// Include this file after <ntl/rpc/server> in the driver, and after
// <ntl/rpc/client> in the user-mode app.

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, a, int, b, {
  return a + b;
})

NTL_ADD_CALLBACK_ID_1(demo_rpc, 0x802, int, negate, int, value, {
  return -value;
})

NTL_RPC_END(demo_rpc)
```

For types with commas, such as `std::map<int, int>`, wrap the type with
`NTL_RPC_ARG_PACK(...)`. For custom objects, provide a `zpp::serializer`
serialization function; the tested `point` class in
[`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp) shows the pattern.

### Kernel Server

```cpp
#include <memory>
#include <string>
#include <ntl/driver>
#include <ntl/rpc/server>
#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto rpc_server = demo_rpc::init(driver);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset();
  });

  return ntl::status::ok();
}
```

`demo_rpc::init(driver)` creates a device named `\Device\demo_rpc`. Keep the
returned `std::shared_ptr<ntl::rpc::server>` alive for as long as the RPC
endpoint should exist. A common pattern is to capture it in the unload callback.

Server callbacks run in the driver's device-control dispatch path. Keep them
short, validate inputs, and use the same IRQL discipline as other
runtime-backed driver control code.

### User-Mode Client

```cpp
#include <exception>
#include <iostream>
#include <ntl/rpc/client>
#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    std::wcout << L"40 + 2 = " << demo_rpc::add(40, 2) << L"\n";

    ntl::rpc::client client(L"demo_rpc");
    auto value = client.invoke<int>(demo_rpc::negate_1_index, 7);
    std::wcout << L"negate(7) = " << value << L"\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "RPC failed: " << e.what() << "\n";
    return 1;
  }
}
```

The generated wrapper functions, such as `demo_rpc::add`, create a temporary
client and call the matching device. Use an explicit `ntl::rpc::client` when
you want to reuse the handle or control the output buffer size.

The driver must be loaded before the app can open the RPC device.

## DPC To Passive Worker

DPC and timer callbacks run at elevated IRQL. They should stay short and avoid
arbitrary STL/CRT work. A common pattern is to let the DPC capture resident
state and use `ntl::passive_executor` for the real work.

```cpp
#include <atomic>
#include <ntl/event>
#include <ntl/passive_executor>
#include <ntl/timer>
#include <ntl/wait>

struct cleanup_context {
  ntl::passive_executor executor;
  ntl::event completed;
  std::atomic<long> value = 0;
};

void on_timer_dpc(void* context, void*, void*) noexcept {
  auto* state = static_cast<cleanup_context*>(context);

  // The DPC remains tiny. The lambda runs later at PASSIVE_LEVEL.
  (void)state->executor.post([state] {
    state->value.store(42);
    state->completed.set();
  });
}

cleanup_context state;
ntl::kdpc dpc(on_timer_dpc, &state);
ntl::timer timer;

timer.set_once(ntl::relative_due_time_ms(10), &dpc);

auto timeout = ntl::relative_timeout_ms(1000);
(void)state.completed.wait(&timeout);
```

The context, executor, DPC, timer, and any captured objects must remain valid
until the timer/DPC has been canceled or drained and the passive work has
completed. This is still normal WDK lifetime management; NTL only makes the
ownership and handoff shape easier to express.

## Raw IOCTL Skeleton

RPC is only a convenience layer. You can still expose a normal device and handle
IOCTLs directly.

### Shared IOCTL Contract

```cpp
// shared/demo_ioctl.hpp
#pragma once

#define DEMO_DEVICE_NAME L"demo_device"

#define DEMO_IOCTL_ECHO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

Include this contract from both sides after the relevant WDK or Windows SDK
headers are available.

### Kernel Device

```cpp
#include <string>
#include <wdm.h>
#include <ntl/driver>
#include "shared/demo_ioctl.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto options = ntl::device_options()
                     .name(DEMO_DEVICE_NAME)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false);

  auto device = driver.create_device<void>(options);
  device->on_device_control([](const ntl::device_control::code& code,
                               const ntl::device_control::in_buffer& in,
                               ntl::device_control::out_buffer& out) {
    if (code == DEMO_IOCTL_ECHO && in.ptr && out.ptr) {
      const auto bytes = in.size < out.size ? in.size : out.size;
      RtlCopyMemory(out.ptr, in.ptr, bytes);
      out.size = bytes;
    }
  });

  driver.on_unload([device]() mutable {
    device.reset();
  });

  return ntl::status::ok();
}
```

If the callback needs extension state, avoid capturing the owning
`std::shared_ptr` inside the callback stored by that same device. Capture a
`std::weak_ptr` instead, as shown in the repository test driver.

### User-Mode App

```cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <ntl/handle>
#include "shared/demo_ioctl.hpp"

int wmain() {
  ntl::unique_handle device{CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\" DEMO_DEVICE_NAME,
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr)};

  if (!device)
    return 1;

  char request[] = "hello";
  char reply[sizeof request] = {};
  DWORD returned = 0;

  const BOOL ok = DeviceIoControl(device.get(),
                                  DEMO_IOCTL_ECHO,
                                  request,
                                  sizeof request,
                                  reply,
                                  sizeof reply,
                                  &returned,
                                  nullptr);

  return ok ? 0 : 1;
}
```

## Practical Notes

- Keep shared RPC and IOCTL contract headers small and stable.
- Do not expose raw kernel pointers, handles, or process-local addresses across
  the app/driver boundary.
- Treat app-provided data as untrusted. Validate sizes, ranges, enum values,
  and serialized object shapes.
- Keep driver-side callbacks in the `PASSIVE_LEVEL` control-path model unless
  the exact path is audited for a broader IRQL contract.
- Make unload ownership explicit. Anything that owns a device, worker thread,
  RPC server, or callback should have a clear teardown point.
