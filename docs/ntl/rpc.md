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
- [`include/ntl/rpc/coroutine`](../../include/ntl/rpc/coroutine), optional C++20

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

Long-running callbacks can use `NTL_ADD_CALLBACK_CONTEXT_0` through
`NTL_ADD_CALLBACK_CONTEXT_5`, or their explicit-ID
`NTL_ADD_CALLBACK_CONTEXT_ID_*` forms. The argument after the callback name is
the kernel-only `ntl::rpc::call_context` variable name:

```cpp
NTL_ADD_CALLBACK_CONTEXT_ID_1(
    demo_rpc, 0x902, std::uint32_t, calculate, call,
    std::uint32_t, count, {
      std::uint32_t result = 0;
      for (std::uint32_t index = 0; index != count; ++index) {
        call.throw_if_cancelled();
        result += calculate_one(index);
      }
      return result;
    })
```

Naming `call` explicitly makes the available request context visible in the
callback body and avoids relying on a hidden macro identifier. The context is
not serialized and is not part of the user-mode function signature. The app
still receives `demo_rpc::calculate(count)` and
`demo_rpc::calculate_1_method`, with the same method ID and wire payload it
would have received from `NTL_ADD_CALLBACK_ID_1`.

Use `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_0` through
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_5`, or their explicit-ID forms, when a
macro-declared method also needs authorization before deserialization. The
argument after the callback name is a server-side policy callable; the next
argument names the `call_context` available to the method callback:

```cpp
NTSTATUS authorize_user_mode(
    const ntl::rpc::call_context& caller) noexcept {
  return caller.is_user_mode() ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_1(
    demo_rpc, 0x903, std::uint32_t, protected_echo,
    authorize_user_mode, call, std::uint32_t, value, {
      call.throw_if_cancelled();
      return value;
    })
```

The policy callable receives the original requester's context and must return
`NTSTATUS` or `ntl::status`. It is a server-only token: the client expansion
discards it and generates the same synchronous, asynchronous, stop-token, and
coroutine wrappers as an ordinary callback declaration. A failed policy status
rejects the request before its argument bytes are decoded or allowed to
allocate memory.

Explicit method IDs must be stable and unique within the endpoint and use the
vendor IOCTL function range `0x800` through `0xFFC`; NTL reserves `0xFFD` for
session control, `0xFFE` for notification receives, and `0xFFF` for contract
discovery. Client arguments are
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
macro-generated `init()` calls `start()` after registration. Use the generated
`demo_rpc::make_server()` instead when notifications or session hooks must be
registered after the macro methods but before startup, and then call `start()`
explicitly. Direct users of `ntl::rpc::make_server()` follow the same rule.
After that point the dispatch
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

Callbacks that perform long-running work can opt into cooperative
cancellation by accepting `ntl::rpc::call_context` before their declared RPC
arguments:

```cpp
server->on(read_values,
    [](ntl::rpc::call_context context, std::uint32_t count) {
      std::vector<std::uint32_t> result;
      result.reserve(count);
      for (std::uint32_t index = 0; index != count; ++index) {
        context.throw_if_cancelled();
        result.push_back(read_one_value(index));
      }
      return result;
});
```

The same callback can be declared in a shared macro contract with the
`NTL_ADD_CALLBACK_CONTEXT_*` family shown earlier. A context-aware declaration
does not turn an endpoint asynchronous by itself. Full asynchronous and
cooperative cancellation behavior requires all three pieces:

1. Configure the endpoint with `server_options::asynchronous()`.
2. Start the call with `client::invoke_async()` to obtain an `async_call` that
   can be waited on, timed out, or cancelled.
3. Use `call_context::cancelled()` or `throw_if_cancelled()` in long-running
   callback work so an already-running callback can stop promptly.

Each callback macro generates both forms. For a method named `read_values`,
`read_values(args...)` is the synchronous convenience function and
`read_values_async(args...)` returns `async_call<T>`:

```cpp
auto call = demo_rpc::read_values_async(std::uint32_t{16});
```

Like the synchronous convenience function, this form opens the named endpoint
for an independent call. Code that makes many calls can keep one
`ntl::rpc::client` and pass the generated `<name>_<arity>_method` descriptor to
`client.invoke()` or `client.invoke_async()` instead.

`cancelled()` becomes true after the request is cancelled or the NTL endpoint
actually begins `stop()`. `throw_if_cancelled()` terminates that RPC with
`STATUS_CANCELLED`, which the app observes as `ERROR_OPERATION_ABORTED`.
Choose a polling interval that bounds cancellation latency without adding a
check to every trivial operation.

The context parameter is optional. Existing `[](args...)` callbacks keep their
original behavior and are selected before the context-aware form. An SCM
service-stop request alone does not start endpoint shutdown while an app still
owns an open legacy WDM device handle; cancel pending calls and close clients
before waiting for `SERVICE_STOPPED`.

`call_context` is a non-owning view valid only while its callback is running.
Do not retain it or capture it in work that can outlive the callback.

Synchronous `invoke()` remains available on both normal and asynchronous
endpoints. On an asynchronous endpoint it waits for the pending operation and
preserves the existing typed return API.

### C++20 Stop Tokens And Coroutines

The existing `invoke_async(method, args...)` overload remains available in
C++14 and later. C++20 clients can bind the same operation to a stop token:

```cpp
#include <stop_token>

std::stop_source source;
auto call = demo_rpc::read_values_async(source.get_token(),
                                        std::uint32_t{16});

source.request_stop();
```

The returned `async_call<T>` owns its `std::stop_callback` registration. A stop
request calls `CancelIoEx` for that operation only. Destruction removes the
registration before the native operation state can be released. If the token
was already stopped, NTL does not issue an RPC and returns an already-cancelled
operation.

Include the optional C++20 adapter to await the same native operation:

```cpp
#include <ntl/rpc/coroutine>

application_task<std::vector<std::uint32_t>>
read_async(std::stop_token token) {
  co_return co_await demo_rpc::read_values_async(token, std::uint32_t{16});
}
```

The generated async function is available in C++14 and later. Its
`std::stop_token` overload and direct `co_await` use require C++20; include
`<ntl/rpc/coroutine>` to make `async_call<T>` awaitable.

`<ntl/rpc/coroutine>` uses a Windows thread-pool wait to resume the coroutine
when the `OVERLAPPED` event is signaled. It does not create one waiting thread
per call, and it does not replace the request buffers, event, device handle, or
`CancelIoEx` ownership held by `async_call<T>`. Moving the call into `co_await`
also preserves its consume-once `get()` semantics.

The C++ standard does not provide a general coroutine `task<T>` return type or
scheduler. Applications use the awaitable with their existing task framework;
the RPC example contains a small top-level task owner solely to make the sample
self-contained. Destroying a coroutine while its RPC awaiter is still pending
prevents later resumption and cancels that request. As with other coroutine
awaitables, an external task owner must not destroy the same coroutine frame
concurrently with a resumption it does not coordinate.

The same C++20 stop-token overload is available for `receive_async()`. C++14
and C++17 translation units do not include `<stop_token>` or `<coroutine>` and
retain the original API and ABI surface. The internal asynchronous operation
also has the same layout in every language mode, so one executable may safely
link C++14 translation units with C++20 translation units that use stop tokens
or the coroutine adapter.

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

## Client Sessions And Reliable Notifications

The notification API above is deliberately transient: the app queues a receive
first, and the driver drops or handles the event itself when no receive exists.
Use an opted-in client session when an event must remain available until that
specific client acknowledges it.

Register the channel and session policies before starting the server:

```cpp
auto server = demo_rpc::make_server(driver, options);
server
    ->register_notification(progress)
    .on_session_open(
        [](ntl::rpc::client_session& session,
           const ntl::rpc::call_context&) -> NTSTATUS {
          session.state(std::make_shared<client_state>());
          return STATUS_SUCCESS;
        })
    .on_session_resume(
        [](ntl::rpc::client_session& session,
           const ntl::rpc::call_context& caller) -> NTSTATUS {
          return authorize_reconnect(session, caller);
        });
server->start();
```

`client_session::state()` stores application-owned typed state across handle
disconnect and reconnect. Session hooks run at `PASSIVE_LEVEL`. The
`call_context` supplied to open and resume policies describes the current
requester, so a reconnect can recheck identity instead of trusting the token as
the only authorization decision. `client_session::token()` lets an application
hook restore its own external authentication or subscription metadata; treat
that token as a secret and never print it. A callback may use the
`client_session` pointer only for the duration of that callback; it must not
retain the pointer. Explicit close and retention expiry wait for active RPC
callbacks associated with the session before `on_session_close()` runs.

The app explicitly creates and subscribes its session:

```cpp
ntl::rpc::client client(L"demo_rpc");
const auto session = client.start_session();
client.subscribe(progress);

auto delivery = client.receive_reliable(progress);
process(delivery.payload());
client.acknowledge(progress, delivery);
```

The driver targets that session's endpoint-local numeric ID:

```cpp
const auto status = server->try_notify(
    session_id, progress, progress_event{42, "indexing", {1, 2, 3}});
```

Reliable delivery has these rules:

- `try_notify(session_id, ...)` serializes and queues one record only for a
  subscribed session. It requires `PASSIVE_LEVEL`.
- `receive_reliable()` and `receive_reliable_async()` return
  `notification_delivery<T>`, which contains the typed payload and its nonzero
  sequence.
- A delivered record stays in the session until `acknowledge()` succeeds.
  Disconnecting before ACK resets its in-flight state, so the next handle that
  resumes the token receives the same sequence again.
- Destroying or closing the device handle disconnects the session; it does not
  discard replay state. Save `session.token` and call `resume_session(token)`
  on a newly opened client.
- `close_session()` explicitly destroys the session and its persisted records.
  Its token cannot be resumed afterward.
- `unsubscribe()` cancels a pending reliable receive. It rejects unsubscribe
  while that channel still has unacknowledged records, preventing silent data
  loss; ACK or process those records first.
- Per-session and endpoint-wide queue limits are configured with
  `max_reliable_notifications_per_session()` and
  `max_reliable_notifications()`. A full queue returns `STATUS_DEVICE_BUSY`,
  providing bounded backpressure rather than unbounded kernel allocation.

The reconnect token is a random 128-bit capability. Treat it as a secret: do
not log it or expose it to unrelated processes. Endpoint ACLs and
`on_session_resume()` remain the policy boundary.

In-memory sessions expire after `session_retention_ms()` while disconnected.
NTL can optionally bridge a longer lifetime through
`notification_storage(std::shared_ptr<notification_store>)`. The store receives
serialized records before publication, ACK removal, token restoration, and
explicit-session deletion. NTL itself performs no file or registry I/O. Store
hooks run at `PASSIVE_LEVEL` without the internal session lock held, and should
be idempotent because disconnect, retry, and shutdown can race with external
storage failures. Retention expiry releases only the in-memory copy; it does
not call `erase_session()`. The external store can therefore restore the token
later when it retained session metadata or at least one unacknowledged record.
Restored records may be supplied in any order. NTL sorts them by the original
session sequence and rejects duplicate sequences, terminal markers on ordinary
notifications, and stream records that appear after that stream's terminal
record. A storage implementation therefore cannot accidentally reopen or
append to a completed stream by returning inconsistent data.

Transient `receive()`/`try_notify()` and session-based reliable delivery are
separate modes on the same typed channel. Existing transient notification code
does not create a session or gain buffering implicitly.

## Typed Streaming

An NTL RPC stream is one session-bound, bidirectional typed channel. The two
directions are independent:

```text
app                                           driver
read_async() waits for output
write(upload) ------------------------------> upload callback
                   stream.write(download) <-- queues one output record
read completes
acknowledge(record) ------------------------> frees queue capacity
                   stream.complete() <------ queues the terminal record
read terminal record, ACK it, then close
```

An app can keep `read_async()` pending while it writes uploads, so upload and
download can progress concurrently. A download does not disappear when no read
is currently pending: it remains in the reliable session queue until the app
reads and explicitly ACKs it. `complete()` and `fail()` are also queued records,
not implicit close operations.

`upload_chunk::finish` below is application data, not an NTL transport flag.
The app knows which chunk is its last input and sets the field before calling
`write()`. This sample protocol lets the driver finish its output after that
last input. A long-lived stream can omit this field and finish according to a
driver-owned condition instead.

Define the serialized types and the stream callback once in the shared contract
header:

```cpp
struct upload_chunk {
  std::uint64_t sequence;
  std::vector<std::uint32_t> values;
  bool finish;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.sequence, self.values, self.finish);
  }
};

struct download_chunk {
  std::uint64_t sequence;
  std::string text;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.sequence, self.text);
  }
};

NTL_RPC_BEGIN_CONTRACT(demo_rpc, 1, 0)

NTL_ADD_STREAM_ID(demo_rpc, 0x920, records,
                  upload_chunk, upload,
                  download_chunk, stream, {
  stream.write(download_chunk{upload.sequence, "accepted"});
  if (upload.finish)
    stream.complete();
})

NTL_RPC_END(demo_rpc)
```

When included after `<ntl/rpc/server>`, this macro registers the driver upload
callback. `stream` is a callback-scoped `stream_context`; it targets the current
client session and exposes `write()`, `complete()`, `fail()`, cancellation, and
the original call context. When included after `<ntl/rpc/client>`, the same
declaration generates `demo_rpc::records_stream` and the
`demo_rpc::records(client)` opening helper.

The app starts a session, opens the generated stream facade, and arms a read
before writing. This makes the independent directions visible in the code:

```cpp
ntl::rpc::client client(L"demo_rpc");
(void)client.start_session();
auto records = demo_rpc::records(client);

auto pending_reply = records.read_async();
records.write(upload_chunk{1, {10, 20, 30}, true});

auto reply = pending_reply.get();
consume(reply.payload().value());
records.acknowledge(reply);

auto terminal = records.read();
if (!terminal.payload().is_completed())
  throw std::runtime_error("stream did not complete");
records.acknowledge(terminal);

records.close();
client.close_session();
```

Use `NTL_ADD_AUTHORIZED_STREAM_ID` when every upload requires an authorization
policy before deserialization. It has the same captured-requester security
semantics as `on_authorized()`. The generated app facade is unchanged.

The direct `ntl::rpc::stream`, `server::on_stream()`, and `client::open_stream()`
APIs remain available when a contract needs custom upload, download, or decode
limits, or when registration cannot be expressed in the shared macro header.

`write_async()` and `read_async()` return the same owned asynchronous operation
types used by ordinary RPC and notification calls. Their `wait_for()`,
`cancel()`, C++20 stop-token, and coroutine rules therefore remain unchanged.
A timeout leaves the underlying I/O request alive; continue waiting, cancel it,
or destroy its owner. Cancel or drain outstanding reads and writes before
`close()` or closing the device handle. A `client_stream` destructor does not
silently close the stream because close can fail while records remain unACKed.

Driver output uses the reliable notification queue. Every successful read must
be ACKed, including terminal records. `try_complete()` queues successful end of
stream; `try_fail()` queues a failed terminal record carrying a WDK
`NTSTATUS`. Disconnect before ACK causes the same sequence to be replayed after
`resume_session()`.

After a terminal record is queued, NTL rejects later uploads, output chunks,
and duplicate terminal records for that session and stream. After the client
ACKs the terminal, another read fails with `STATUS_END_OF_FILE`; the client
then calls `close()`. Closing removes that stream instance, so the same session
may open a fresh instance afterward. Disconnect before terminal ACK preserves
both the terminal sequence and the non-writable state for reconnect replay.

Calls made serially by one producer are queued in that order. If multiple
callbacks publish to the same session and stream concurrently, use an
application sequence field to define their merge order. A terminal waits
behind already-started output publication and prevents new publication from
starting.

Backpressure is bounded in both directions:

- Downloads consume the configured per-session and endpoint reliable queue
  limits. `try_write()`, `try_complete()`, and `try_fail()` return
  `STATUS_DEVICE_BUSY` while the queue is full. An ACK releases capacity. A
  failed terminal enqueue rolls its reservation back, so the stream remains
  writable and the producer can retry after capacity is released.
- Uploads are ordinary limited RPC requests. Serialized request and decode
  budgets come from the stream descriptor, while asynchronous concurrency is
  bounded by `server_options::max_pending_calls()`.

### Bounded batches and delivery priority

The single-record `write()`, `read()`, `read_async()`, and `acknowledge()` APIs
remain the default. A stream can also move several serialized records through
one IOCTL without changing its element types:

```cpp
std::vector<upload_chunk> uploads{first, second, last};
records.write_batch(uploads);

auto downloads = records.read_batch(4);
for (const auto& delivery : downloads.values()) {
  consume(delivery.payload());
}
records.acknowledge(downloads);
```

`write_batch()` invokes the registered upload callback once per element, in
vector order, and checks cooperative cancellation between elements. It is not
a transaction: if a later callback fails, effects from earlier callbacks are
not rolled back. Empty batches and batches larger than the descriptor's
`max_batch_records()` are rejected. The serialized request and cumulative
decode allocation must still fit the stream's ordinary upload limits.

`read_batch()` returns up to the requested number of records that are already
ready. It does not wait for the batch to fill. Every returned record retains
its own sequence and remains replayable until ACK. The batch ACK convenience
function sends those ACKs in order; it is likewise not atomic, so an error does
not undo earlier ACKs. Timeout, `CancelIoEx`, stop-token, reconnect, terminal,
and persistence behavior are the same as for a single reliable read.

The compile-time bound defaults to 16 and cannot exceed 64:

```cpp
constexpr auto records =
    ntl::rpc::stream<0x920, upload_chunk, download_chunk>{}
        .with_batch_records<8>()
        .with_priority<ntl::rpc::delivery_priority::high>();
```

Priority matters only when one any-channel batch receive selects among records
that are ready on several subscribed reliable channels. A channel-specific
`read()` or `read_batch()` already names its channel and therefore has nothing
to prioritize. Within one channel, NTL always preserves sequence/FIFO order.
For an any-channel consumer:

```cpp
auto batch = client.receive_reliable_batch(8, 64 * 1024);
for (const auto& item : batch.items()) {
  if (item.id() == critical_events.id()) {
    auto event = item.decode(critical_events);
    process(event.payload());
  }
  client.acknowledge(item);
}
```

The selector compares `critical`, `high`, `normal`, then `background`; records
with the same priority are selected by their session sequence. A pending batch
completes as soon as at least one record is ready, so it does not delay a low
priority record in the hope that a future high priority record might arrive.
The `max_records` and `max_bytes` arguments bound kernel and app memory for one
receive.

This stream transports serialized chunks through `METHOD_BUFFERED`; it is not
a zero-copy data path. Shared-memory or MDL-backed transfer has different
mapping, process-exit, cancellation, and driver-unload ownership requirements
and is therefore an explicit optional data plane rather than an invisible
change to this serialized stream contract.

## Shared-Memory Data Plane

For high-volume fixed-layout records, an opted-in client session can register
caller-owned memory with `client::register_shared_region()`. The driver probes,
pins, and maps the pages in the original requestor process context and returns a
fixed-width `region_handle`. RPC methods exchange only `buffer_token` values;
callbacks resolve them through `call_context::try_resolve()` with explicit
driver-read or driver-write access.

The common `ntl::ipc::shared_ring<T, Capacity>` layout provides bounded SPSC
backpressure without serializing each record through `DeviceIoControl`. Use one
ring per direction for duplex traffic. Record fields must be fixed-width and
must not contain pointers, handles, `size_t`, strings, or STL containers.

Shared regions require a client session. They are invalidated on unregister,
disconnect, session close, retention expiry, and server stop. Per-session count
and byte quotas are configured through `server_options`. See
[`IPC shared memory`](./ipc.md) for the API and lifetime rules, and
[`test/rpc/cross-bitness`](../../test/rpc/cross-bitness) for the x64-driver with
x64/x86-client VM coverage.

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
cooperative cancellation of a running callback, request ownership beyond
client lifetime, concurrent overlapped calls, pending resource limits, and
cleanup of an abandoned request.

The C++20 client tests additionally cover typed and `void` coroutine results,
already-stopped and running stop tokens, exactly-once resumption, abandonment
of a suspended coroutine frame, and reuse of the endpoint after cancellation.

[`test/rpc/notifications`](../../test/rpc/notifications) covers typed STL
payloads, FIFO receives, timeout and cancellation, pending receive limits,
handle cleanup, service stop after pending receive cancellation, unload,
restart, reconnectable sessions, replay until ACK, bounded backpressure,
persistence hooks, and x64-driver/x86-app wire compatibility.

[`test/rpc/streaming`](../../test/rpc/streaming) covers session-bound typed
uploads and downloads, explicit ACK, bounded backpressure and capacity
recovery, timeout and cancellation, callback cancellation, reconnect replay,
terminal records, reverse-order persistence restoration, bounded upload and
download batches, cross-channel priority selection, and the x64-driver/x86-app
wire contract.

[`test/rpc/security`](../../test/rpc/security) covers the original caller's
processor mode, process ID, impersonation token, and security subject context.
It runs the server asynchronously to verify that the referenced caller tokens
remain valid after dispatch moves from the request thread to a system worker.
The fixture also verifies allow and deny security descriptors, callback
suppression after authorization failure, and x64-driver/x86-app wire
compatibility.

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

For per-method authorization, register the typed method with
`on_authorized()`. The policy runs before request deserialization and before
the application callback:

```cpp
constexpr ACCESS_MASK read_report = 0x0001;
GENERIC_MAPPING report_mapping{
    read_report, read_report, read_report, read_report};

server->on_authorized(
    read_report_method,
    [&](const ntl::rpc::call_context& call) {
      return call.check_access(report_security_descriptor,
                               read_report,
                               report_mapping);
    },
    [](std::uint32_t report_id) {
      return load_report(report_id);
    });
```

An authorization policy must return `NTSTATUS` or `ntl::status`. A failed
status is returned to the app without decoding the method arguments or running
the method callback. `bool` is deliberately rejected because a C++ `true`
would otherwise be interpreted as a successful nonzero `NTSTATUS` value.
Macro schemas use the equivalent
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT[_ID]_*` forms described above; they retain
the same ordering and security guarantee as `on_authorized()`.

`ntl::rpc::call_context` also provides `requestor_mode()`, `is_user_mode()`,
`is_kernel_mode()`, and `requestor_process_id()`. The process ID is diagnostic
metadata only: IDs can be reused and must not be used as credentials. Advanced
policies can pass `native_subject_context()` to documented Windows security
APIs such as `SePrivilegeCheck`. Treat the structure as opaque: do not inspect
or modify its members. The returned pointer is non-owning, must not be released
or retained, and is valid only during the policy or method callback. Prefer
`check_access()` for ordinary access decisions.

Both synchronous and asynchronous servers capture the subject context while
handling the original IRP. Asynchronous dispatch therefore evaluates the
original client's primary or impersonation token rather than the system worker
thread's token. Subject capture, authorization, and method callbacks require
`PASSIVE_LEVEL`.
