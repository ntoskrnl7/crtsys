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

NTL_RPC_BEGIN_CONTRACT(demo_rpc, 3, 0x1ull)

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
vendor IOCTL function range `0x800` through `0xFFD`; NTL reserves `0xFFE` for
notification receives and `0xFFF` for contract discovery. Client arguments are
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
the same method ID twice fails instead of silently replacing a callback. The
macro-generated `init()` calls `start()` after registration; direct users of
`make_server()` must call `start()` explicitly. After that point the dispatch
table is immutable, so late registration is rejected instead of racing with
active calls.

Server callbacks run in the driver's device-control dispatch path. Keep them
short, validate semantic limits before allocating, and follow the same IRQL and
lifetime rules as other driver control callbacks. Server shutdown rejects new
calls and waits for callbacks that already acquired rundown protection. Do not
call `stop()` from one of the server's own callbacks because it would wait for
that callback to return.

`stop()` leaves the control device owned by the server while rejecting its RPC
dispatches. Keep the server alive until driver unload, as shown above. This
allows existing user handles to close before the server destructor releases the
NTL device and its C++ extension state.

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

## Asynchronous Calls, Timeouts, And Cancellation

Asynchronous calls are opt-in on the endpoint. The server pends application
method IRPs and invokes their callbacks from PASSIVE_LEVEL system work items.
Contract discovery remains synchronous.

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.asynchronous().max_pending_calls(64);

// Macro contracts accept the same options while retaining their generated
// callback registration.
auto server = demo_rpc::init(driver, options);
```

The pending-call limit bounds retained IRPs, nonpaged request state, and queued
work. The default is `server_options::default_max_pending_calls`; choose a
smaller product-specific value when the callbacks are expensive. An excess
call fails before its application callback runs.

The user-mode client owns every input/output buffer, event, and `OVERLAPPED`
structure until completion:

```cpp
using namespace std::chrono_literals;

ntl::rpc::client client(L"demo_rpc");
auto call = client.invoke_async(demo_rpc::read_values_1_method,
                                std::uint32_t{16});

if (call.wait_for(250ms) == ntl::rpc::async_wait_status::timeout) {
  (void)call.cancel();
}

const auto values = call.get(); // waits, then deserializes or throws
```

`wait_for()` reporting `timeout` does not release the live request. The caller
may keep waiting, call `cancel()`, or destroy the `async_call`; destruction
requests cancellation and drains completion before releasing its buffers. An
`async_call` may outlive the `client` that created it because both retain the
same device handle.

`CancelIoEx` cannot forcibly stop arbitrary kernel C++ code. If cancellation
wins before a queued callback starts, NTL skips that callback. If the callback
is already running, NTL lets it return, discards its output, and completes the
IRP with `STATUS_CANCELLED`; `get()` then throws `std::system_error` with
`ERROR_OPERATION_ABORTED`. Callback code should therefore remain bounded and
must not treat cancellation as thread termination.

Synchronous `invoke()` remains available on both normal and asynchronous
endpoints. On an asynchronous endpoint it waits for the pending operation and
preserves the existing typed return API.

## Kernel-To-App Notifications

A notification channel describes one serializable payload shared by the
driver and app. Its ID is a fixed-width application channel ID, not a native
pointer or an architecture-sized value:

```cpp
struct progress_event {
  std::uint64_t operation_id = 0;
  std::string phase;
  std::vector<std::uint32_t> values;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.operation_id, self.phase, self.values);
  }
};

constexpr auto progress =
    ntl::rpc::notification<0x1001, progress_event>{}
        .max_response_size<64 * 1024>()
        .max_decode_allocation<128 * 1024>();
```

Register every channel before `start()`. Publishing requires `PASSIVE_LEVEL`
because arbitrary payload serialization can execute CRT/STL code:

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.max_pending_notifications(32);

auto server = ntl::rpc::make_server(driver, options);
server->register_notification(progress);
server->start();

const auto status = server->try_notify(
    progress, progress_event{42, "indexing", {1, 2, 3}});
if (static_cast<NTSTATUS>(status) == STATUS_NOT_FOUND) {
  // No app currently has a receive queued. NTL does not buffer the event.
}
```

The app queues a receive before asking the driver to produce the event:

```cpp
using namespace std::chrono_literals;

ntl::rpc::client client(L"demo_rpc");
auto receive = client.receive_async(progress);

if (receive.wait_for(250ms) == ntl::rpc::async_wait_status::timeout)
  (void)receive.cancel();

const auto event = receive.get();
```

`receive(progress)` provides the blocking form. `receive_async(progress)`
returns `notification_wait<payload_type>`, which has the same `ready()`,
`wait_for()`, `wait()`, `cancel()`, and `get()` ownership rules as
`async_call<T>`.

The transport is an inverted-call queue: each app receive contributes one
pending `METHOD_BUFFERED` IRP, and each successful `try_notify()` completes the
oldest receive for the matching channel. It is intentionally queue delivery,
not broadcast delivery. Multiple independent consumers therefore compete for
events on the same endpoint. A product that needs broadcast semantics should
create separate endpoints or implement subscriber identity in its application
contract.

NTL uses `IO_CSQ` for cancellation races. `CancelIoEx`, destruction of a
`notification_wait`, `IRP_MJ_CLEANUP`, and server shutdown each remove and
complete a receive exactly once. `stop()` first rejects new receives, completes
all queued receives with a failure, and then waits for normal RPC callback
rundown.

A legacy WDM driver cannot finish a service stop while an app still owns an
open device handle. Before waiting for `SERVICE_STOPPED`, cancel or destroy all
`notification_wait` objects and close their clients. `IRP_MJ_CLEANUP` then
removes the app's queued receives, allowing unload to begin; the server shutdown
flush remains the final defense for any receive still owned by the endpoint.

The pending receive limit bounds retained IRPs and their I/O-manager buffers.
An event is not retained when no receive exists; `try_notify()` returns
`STATUS_NOT_FOUND`, allowing the driver to drop, count, coalesce, or queue the
event using product-specific policy. Payload bytes contain no extra framework
header. The receive request carries only one hidden `std::uint32_t` channel ID,
so the same x64 driver contract is usable by x86 and x64 apps.

## Contract Compatibility

An app and driver that are released independently should validate their shared
contract once after opening the endpoint. Contract discovery is a reserved
query, not a header added to every RPC request:

```cpp
ntl::rpc::client client(L"demo_rpc");

ntl::rpc::contract_requirements requirements;
requirements
    .contract_version(demo_rpc::rpc_contract_version)
    .transport_features(ntl::rpc::transport_features::resource_limits |
                        ntl::rpc::transport_features::secure_endpoint)
    .capabilities(demo_rpc::rpc_capabilities)
    .method(demo_rpc::add_2_method)
    .method(demo_rpc::read_values_1_method);

const auto server_contract = client.require_contract(requirements);
```

`NTL_RPC_BEGIN_CONTRACT(Name, Version, Capabilities)` publishes the application
contract version, application-defined capability bits, and every registered
method ID. `NTL_RPC_BEGIN(Name)` remains the compact form and publishes version
`1` with no application capability bits.

The reported values have separate meanings:

- `protocol_version()` is the NTL wire format version. NTL validates it
  automatically in `require_contract()`.
- `contract_version()` is the application's exact schema/API version.
- `transport_feature_mask()` describes NTL protections such as resource limits,
  endpoint security, immutable dispatch, and rundown shutdown.
- `capability_mask()` contains application-defined optional feature bits.
- `method_ids()` is the sorted set of methods registered before `start()`.

`query_contract()` returns this metadata without imposing application policy.
`require_contract()` checks selected requirements and throws
`ntl::rpc::contract_mismatch`; its `reason()`, `expected()`, and `actual()`
members distinguish protocol, application version, transport feature,
capability, and missing-method failures. This lets an app report a clear
driver/app version mismatch before invoking a business method.

Use explicit `NTL_ADD_CALLBACK_ID_*` IDs for independently updated binaries.
Line-derived IDs are convenient when both sides always build from the same
header, but source reordering changes those IDs and therefore is not a stable
published contract.

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

## Request And Decode Limits

Every method also limits serialized request bytes and cumulative dynamic
storage created while decoding. The defaults are 1 MiB per request and 4 MiB
of decoded dynamic storage. Methods that accept variable-size input should set
limits appropriate for their contract:

```cpp
constexpr auto update_words =
    ntl::rpc::method<0x901, void(const std::vector<std::string>&)>{}
        .max_request_size<64 * 1024>()
        .max_decode_allocation<256 * 1024>();
```

The shared callback macros expose the same contract without abandoning the
single-source driver/client declaration:

```cpp
NTL_ADD_CALLBACK_ID_1(
    demo_rpc, 0x901,
    NTL_RPC_METHOD_LIMITS(0, 64 * 1024, 256 * 1024, void),
    update_words, const std::vector<std::string>&, words, {
      apply_words(words);
    })
```

The three numeric values are response capacity, serialized request limit, and
decode-allocation budget, in that order. A `void` method ignores the response
capacity; use `0` to make that explicit.

The request limit rejects an oversized `DeviceIoControl` input before decoding
or invoking the callback. The decode-allocation limit is separate because a
small payload can declare a very large container count. NTL charges dynamic
containers, including nested and associative containers, against this budget
before allocation. The same method budget protects client-side response
decoding.

These are transport resource limits, not application validation. A callback
must still validate semantic rules such as maximum item count, string length,
allowed enum values, and authorization for the requested operation.

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
| Boundary cases | Empty and large payloads, undersized responses, truncated data, invalid lengths, trailing bytes, request/decode limits, and unknown method IDs |

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
request or decode-budget violations, undersized output buffers, and unknown
method IDs before invoking application code. The client deserializes only the
byte count returned by `DeviceIoControl` and rejects trailing response bytes or
a reply that cannot construct the declared return type.

See [`test/rpc/cross-bitness`](../../test/rpc/cross-bitness) for the buildable
x64 driver plus x86/x64 client fixture. The x64-driver/x86-client VM run is the
authoritative cross-bitness case; the x64 client is the same-bitness control.

[`test/rpc/lifecycle-stress`](../../test/rpc/lifecycle-stress) repeatedly opens
and closes short-lived clients, stops the driver service while a long callback
is active, restarts it, and validates a fresh contract and call. Run that
fixture under Driver Verifier to cover unload and device lifetime races that a
single process execution cannot expose.

[`test/rpc/async`](../../test/rpc/async) covers timeout, targeted cancellation,
request ownership beyond client lifetime, concurrent overlapped calls, pending
resource limits, and cleanup of an abandoned request.

[`test/rpc/notifications`](../../test/rpc/notifications) covers typed STL
payloads, FIFO receives, timeout and cancellation, pending receive limits,
handle cleanup, service stop after pending receive cancellation, unload,
restart, and x64-driver/x86-app wire compatibility.

## Trust Boundary

NTL RPC IOCTLs require a handle opened for both read and write access. By
default, `make_server()` creates a named device with `IoCreateDeviceSecure()`
and an ACL that grants access only to Local System and Administrators. The
cross-bitness fixture verifies that a restricted impersonation token cannot
open the endpoint.

Use `server_options` when a product needs a different principal set. Give the
device class a project-owned GUID rather than reusing the NTL default:

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.security_descriptor(project_sddl, project_device_class_guid);

auto server = ntl::rpc::make_server(driver, options);
server->on(method, callback);
server->start();
```

The ACL controls who can open the device. Serialization checks and resource
limits control how accepted bytes are decoded. Neither replaces per-method
authorization or semantic validation when callers have different privileges.
