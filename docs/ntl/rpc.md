# NTL RPC

[Back to NTL docs](./README.md)

NTL RPC generates a kernel callback dispatcher and user-mode wrapper functions
from one shared macro declaration over `DeviceIoControl`. Its implementation is
backed by typed method descriptors that carry the IOCTL ID, argument
types, return type, and maximum serialized response size. Serialization uses
`zpp::serializer`.

For a buildable driver/app pair, see
[`examples/ntl-rpc-driver`](../../examples/ntl-rpc-driver).

Headers:

- [`include/ntl/rpc/common`](../../include/ntl/rpc/common)
- [`include/ntl/rpc/server`](../../include/ntl/rpc/server)
- [`include/ntl/rpc/client`](../../include/ntl/rpc/client)

## Shared Contract And Implementation

```cpp
// shared/demo_rpc.hpp
#pragma once

#include <cstdint>
#include <vector>

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, left, int, right, {
  return left + right;
})

NTL_ADD_CALLBACK_1(
    demo_rpc,
    NTL_RPC_BOUNDED_RESPONSE(64 * 1024, std::vector<std::uint32_t>),
    read_values, std::uint32_t, count, {
      if (count > 4096)
        throw ntl::exception(STATUS_INVALID_PARAMETER, "count is too large");
      return std::vector<std::uint32_t>(count, 42);
    })

NTL_RPC_END(demo_rpc)
```

Include `<ntl/rpc/server>` before this header in the driver and
`<ntl/rpc/client>` before it in the app. In the driver, each macro body becomes
the registered kernel callback. In the app, the same declaration becomes a
typed wrapper such as `demo_rpc::add(left, right)`; the kernel-only body is not
compiled into the app.

The suffix in `NTL_ADD_CALLBACK_0` through `NTL_ADD_CALLBACK_5` is the callback
argument count. The macro uses it to generate named parameters and
serialization code on both sides. These default forms derive an ID from the
shared schema line and are the convenient choice when the driver and app build
from the same contract header.

Use `NTL_ADD_CALLBACK_ID_0` through `NTL_ADD_CALLBACK_ID_5` when independently
versioned driver and app binaries must retain the same method IDs even after
the schema is reformatted or reordered. This is an optional ABI-stability
control, not the required form for normal shared-header use.

Explicit method IDs must be stable and unique within the endpoint and use the
vendor IOCTL function range `0x800` through `0xFFF`. Client arguments are
converted to the declared argument types before serialization, so a fixed-width
method declaration cannot accidentally encode a native-width caller type.

For custom objects, provide a `zpp::serializer` serialization function. The
tested `point` class in
[`test/cmake/common/rpc.hpp`](../../test/cmake/common/rpc.hpp) shows the
pattern.

## Kernel Server

```cpp
#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>

#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto server = demo_rpc::init(driver);

  driver.on_unload([server]() mutable { server.reset(); });
  return ntl::status::ok();
}
```

`demo_rpc::init()` creates `\Device\demo_rpc` and registers every callback in
the shared declaration. Keep the returned
`std::shared_ptr<ntl::rpc::server>` alive for the endpoint lifetime. Registering
the same method ID twice fails instead of silently replacing a callback.

Server callbacks run in the driver's device-control dispatch path. Keep them
short, validate semantic limits before allocating, and follow the same IRQL and
lifetime rules as other driver control callbacks.

## User-Mode Client

```cpp
#include <exception>
#include <iostream>

#include <ntl/rpc/client>

#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    ntl::rpc::client client(L"demo_rpc");
    if (!client)
      return 1;

    // Generated wrapper: opens the endpoint and performs one typed call.
    std::cout << demo_rpc::add(40, 2) << '\n';

    // Reusable handle: every generated callback also exposes its method object.
    const auto values = client.invoke(demo_rpc::read_values_1_method,
                                      std::uint32_t{16});
    return values.size() == 16 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "RPC failed: " << error.what() << '\n';
    return 1;
  }
}
```

The generated wrapper is the shortest path for occasional calls. Reuse an
`ntl::rpc::client` and the generated `<name>_<arity>_method` object for repeated
calls without reopening the device. `invoke()` derives its return type from the
method object, so callers do not repeat an ID or return type.

## Bounded Responses

Every non-void method has a maximum serialized response size. The default is
4 KiB; variable-size methods should set a realistic limit with
`NTL_RPC_BOUNDED_RESPONSE(Bytes, ReturnType)`.

This is a receive-buffer contract, not a protocol header:

1. The client allocates the declared capacity locally and passes it as the
   `DeviceIoControl` output buffer.
2. The server verifies the full capacity before invoking the callback. A
   too-small or mismatched client therefore cannot trigger callback side
   effects and then fail while writing the reply.
3. The server serializes directly into that bounded output buffer and returns
   only the bytes actually written.
4. A result larger than the declared limit fails with a buffer error; it is
   never truncated or completed from unused receive-buffer bytes.

There is no extra version or size header in every payload and no probe/retry
round trip. The cost is the local receive-buffer allocation selected for that
method. Keep limits large enough for valid replies but small enough to bound
untrusted allocation pressure.

## Typed Core

The macro frontend delegates to `ntl::rpc::method<Id, Signature>` and
`ntl::rpc::server::on()`. Advanced code and focused tests may use these types
directly, but ordinary driver/app contracts should prefer the macros because
they generate the server registration and user wrapper from the same source.

## Validated Payload Types

The x64-driver/x86-client fixture validates these payload categories across
the user/kernel and process-bitness boundary:

| Category | Validated types and cases |
| --- | --- |
| Scalars | Fixed-width integers, floating-point values, `bool`, and fixed-width enums |
| Text | `std::string`, `std::wstring`, embedded nulls, and empty strings |
| Containers | Sequence, set, map, unordered, nested, and fixed-size containers |
| Compound values | `std::pair`, `std::tuple`, `std::optional`, and `std::variant` |
| User types | Custom objects with `zpp::serializer` serialization support |
| Boundary cases | Empty and large payloads, undersized responses, truncated data, invalid lengths, trailing bytes, and unknown method IDs |

This table records exercised coverage, not an exhaustive allowlist. Other
serializable types can work, but cross-bitness schemas must follow the wire
rules below.

## Cross-Bitness Wire Contract

An x86 app can call an x64 NTL RPC driver. Use fixed-width integer types such
as `std::uint32_t` and `std::uint64_t` for wire-visible sizes, offsets,
identifiers, and handles represented as data.

Do not place `std::size_t`, `std::ptrdiff_t`, `std::uintptr_t`, `ULONG_PTR`, raw
pointers, or structures containing pointers directly in a cross-bitness RPC
contract. Their encoded size differs between x86 and x64. Convert their
semantic value to a fixed-width field at the method boundary instead. STL
containers are safe only when their element and nested value types follow the
same rule; the container's process-local representation is never transmitted.

The server rejects unread trailing input, impossible container lengths,
undersized output buffers, and unknown method IDs before invoking application
code. The client deserializes only the byte count returned by
`DeviceIoControl` and rejects trailing response bytes or a reply that cannot
construct the declared return type.

See [`test/rpc/cross-bitness`](../../test/rpc/cross-bitness) for the buildable
x64 driver plus x86/x64 client fixture. The x64-driver/x86-client VM run is the
authoritative cross-bitness case; the x64 client is the same-bitness control.

## Trust Boundary

The current endpoint uses `FILE_ANY_ACCESS` and is intended for a controlled
local driver/client pair. RPC serialization validation prevents malformed
buffers from becoming ordinary application inputs, but it is not authorization.
Drivers exposed to untrusted processes must apply an appropriate device security
descriptor and validate caller permissions and semantic input limits.
