# NTL RPC Sample Driver

This sample shows the NTL RPC helper layer. It is intentionally separate from
the typed IOCTL sample in `examples/ntl-driver`: the IOCTL sample shows manual
device-control contracts, while this sample generates both sides from one
shared callback declaration.

The sample demonstrates:

- `ntl::main` as the C++ driver entry point
- `ntl::rpc::server` lifetime owned by the driver unload callback
- `ntl::rpc::client` from a user-mode companion app
- shared-schema RPC callback IDs and deduced return types
- direct generated wrappers plus a reusable typed client
- an asynchronous `OVERLAPPED` RPC that leaves the calling thread available
- timeout, `CancelIoEx`, and cooperative kernel callback cancellation
- bounded variable-size responses checked before server callback execution
- a secure control device restricted to Local System and Administrators by
  default
- per-method request and decode-allocation limits
- an immutable dispatch table with rundown-protected shutdown
- startup contract discovery for version, capability, and method compatibility
- original caller identity and pre-deserialization method authorization
- reconnectable client sessions and reliable notifications replayed until ACK
- session-bound typed streaming with bounded backpressure
- serialization of simple scalar values, a custom request/reply pair, and a
  `std::vector`

The source is split by responsibility:

- `shared/ntl_rpc_sample_types.hpp`: serialized request and reply types
- `shared/ntl_rpc_sample.hpp`: the driver/app RPC contract
- `shared/ntl_rpc_caller_security.hpp`: caller-security method descriptors
- `driver/caller_security.cpp`: caller inspection and method authorization
- `driver/operations.cpp`: kernel callback implementations
- `driver/notifications.cpp`: session state and reliable notification publish
- `shared/ntl_rpc_sample.hpp`: generated RPC methods and typed stream callback
- `app/caller_security.cpp`: caller-security client calls
- `app/synchronous_calls.cpp`: generated wrappers and reusable synchronous client
- `app/asynchronous_call.cpp`: successful asynchronous completion
- `app/cancellation.cpp`: timeout followed by cooperative cancellation
- `app/coroutine_call.cpp`: C++20 `co_await` completion
- `app/stop_token_cancellation.cpp`: C++20 `stop_token` cancellation
- `app/reliable_notifications.cpp`: subscribe, reconnect, replay, and ACK
- `app/streaming.cpp`: open, write, read, ACK, and close a typed stream
- `app/streaming_batch.cpp`: bounded multi-chunk upload and download batches
- `app/coroutine_task.hpp`: minimal top-level coroutine owner used by the app
- `app/main.cpp`: argument parsing, contract validation, and example sequencing

## Visual Studio / NuGet

Open [`crtsys_ntl_rpc_sample_vs.sln`](./crtsys_ntl_rpc_sample_vs.sln) in Visual
Studio with the WDK workload installed. The solution contains:

- `crtsys_ntl_rpc_sample`: the kernel RPC server driver
- `crtsys_ntl_rpc_sample_app`: the user-mode RPC client app

Restore NuGet packages, then build `Debug|x64` or `Release|x64`. The project
files use:

```xml
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysPackageVersion` defaults to `*`, so NuGet restore selects the latest
stable `crtsys` package from the configured package sources. For reproducible
builds, pin an exact package version from MSBuild:

```bat
msbuild crtsys_ntl_rpc_sample_vs.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CrtSysPackageVersion=0.1.32
```

## CMake Build

From the repository root:

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64
cmake --build examples\ntl-rpc-driver\build_x64 --config Debug
```

The Debug output is:

```text
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample.sys
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe
```

To suppress diagnostic breakpoints while experimenting:

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## Shared Schema

The shared contract lives in
[`shared/ntl_rpc_sample.hpp`](./shared/ntl_rpc_sample.hpp). The macro bodies
become kernel callbacks when included after `<ntl/rpc/server>` and typed
user-mode wrappers when included after `<ntl/rpc/client>`.

The schema exposes:

- `crtsys_ntl_rpc_sample::add`
- `crtsys_ntl_rpc_sample::describe`
- `crtsys_ntl_rpc_sample::series`
- `crtsys_ntl_rpc_sample::delayed_add`

The schema uses `NTL_RPC_BEGIN_CONTRACT` to publish application contract
version `2`, sample capability bits, and stable explicit method IDs. The app
calls `client.require_contract()` once before its first generated wrapper, so a
mismatched driver fails with a contract diagnostic instead of an unrelated
method error.

The shared header maps each method to an implementation in
`driver/operations.cpp`. The driver-side `describe` operation intentionally
calls the kernel-only WDK API `KeGetCurrentIrql()` and returns that value as
`server_irql`. This makes the execution boundary visible: the app only receives
the serialized reply. The operations also emit one-line `DbgPrint` messages so
a kernel debugger can see that they ran in the driver.

The `series` callback uses `NTL_RPC_BOUNDED_RESPONSE` to declare a 64 KiB
maximum serialized response. The client uses that value as its receive
capacity, and the server verifies the capacity before executing the callback.
Use `NTL_RPC_METHOD_LIMITS` instead when a method also needs request and
decode-allocation limits smaller than the secure defaults documented in
[`docs/ntl/rpc.md`](../../docs/ntl/rpc.md).

The macro-generated server freezes its dispatch table after registering the
shared schema. Keep the returned server owner alive until driver unload. NTL
rejects new RPC calls during shutdown and waits for callbacks already in
progress before the owner releases the device.

This sample uses `crtsys_ntl_rpc_sample::make_server()` instead of `init()` so
it can register its notification channel and session callback before
`start()`. The ordinary `init()` convenience API remains appropriate when a
contract needs only its macro-declared methods.

## Reliable Notification At A Glance

[`app/reliable_notifications.cpp`](./app/reliable_notifications.cpp) creates a
session, subscribes to `progress`, receives a typed delivery, and intentionally
disconnects before ACK. A second client resumes the opaque token, receives the
same sequence, acknowledges it, and explicitly closes the session:

```cpp
const auto session = client.start_session();
client.subscribe(crtsys_ntl_rpc_sample::progress);

const auto delivery =
    client.receive_reliable(crtsys_ntl_rpc_sample::progress);
client.acknowledge(crtsys_ntl_rpc_sample::progress, delivery);
```

The exhaustive queue-limit, cancellation, x86-app/x64-driver, and lifecycle
cases stay in [`test/rpc/notifications`](../../test/rpc/notifications) so the
public sample remains readable.

## Typed Streaming

[`app/streaming.cpp`](./app/streaming.cpp) demonstrates the complete normal
path without mixing stress logic into the sample. The shared contract macro
generates the driver upload callback registration and the app-side opening
helper from one declaration:

```cpp
NTL_ADD_STREAM_ID(
    crtsys_ntl_rpc_sample, 0x905, messages,
    ntl_rpc_sample_stream_upload, upload,
    ntl_rpc_sample_stream_download, stream, {
      ntl_rpc_sample_stream_download reply;
      reply.sequence = upload.sequence;
      reply.text = "driver received: " + upload.text;
      stream.write(reply);
      if (upload.finish)
        stream.complete();
    })
```

`upload.finish` belongs to this sample's chunk format. The app sets it only on
its final upload chunk; it is not inferred or injected by NTL. The driver uses
that application-level end marker to queue its own terminal output record.

At runtime, the two directions are independent:

```text
app read_async() waits
app write(upload) -------------------------> driver callback
                       stream.write(reply) -> app download queue
app read completes, then app ACKs the reply
                       stream.complete() --> app terminal record
app ACKs the terminal record and closes the stream
```

The app keeps one overlapped download read armed while each upload is sent:

```cpp
(void)client.start_session();
auto messages = crtsys_ntl_rpc_sample::messages(client);
auto pending_download = messages.read_async();

ntl_rpc_sample_stream_upload upload;
upload.sequence = 1;
upload.text = "chunk 1";
upload.finish = true;
messages.write(upload);

auto reply = pending_download.get();
// Process reply.payload().value() here.
messages.acknowledge(reply);

auto terminal = messages.read();
if (!terminal.payload().is_completed())
  throw std::runtime_error("stream did not complete");
messages.acknowledge(terminal);

messages.close();
client.close_session();
```

Downloads remain queued until ACK and are bounded by the server's reliable
notification limits. Upload calls use the method request/decode limits and the
endpoint pending-call limit. Timeout, cancellation, reconnect replay, terminal
records, and cross-bitness cases are kept in
[`test/rpc/streaming`](../../test/rpc/streaming).

Once the driver queues `complete()` or `fail()`, NTL rejects later uploads,
downloads, and duplicate terminal records for that stream instance. The app
ACKs the terminal and closes the stream before opening another instance.

[`app/streaming_batch.cpp`](./app/streaming_batch.cpp) sends three upload
chunks through one `write_batch()` call and drains ready replies with
`read_batch(4)`. A batch read completes when at least one record is ready; it
does not wait for all four slots. Each record keeps its own replay/ACK sequence,
and `messages.acknowledge(batch)` acknowledges them in order.

The sample initializes the endpoint with `server_options::asynchronous()`.
Application requests are therefore pended and executed by PASSIVE_LEVEL work
items. `delayed_add` uses `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_3`, which
checks the original caller before decoding the request and gives its kernel
implementation a named `ntl::rpc::call_context`. The operation polls that
context between short waits and exits through `throw_if_cancelled()` when the
app cancels the request. The generated app API remains
`crtsys_ntl_rpc_sample::delayed_add(...)` and `delayed_add_async(...)`.

## Caller Authorization At A Glance

[`driver/caller_security.cpp`](./driver/caller_security.cpp) keeps the complete
authorization path together. The policy receives the original requester even
though the sample server executes the call on a system worker thread:

```cpp
server->on_authorized(
    crtsys_ntl_rpc_security::user_mode_echo,
    [](const ntl::rpc::call_context &caller) -> NTSTATUS {
      return caller.is_user_mode() ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
    },
    [](std::uint32_t value) { return value; });
```

The app invokes the same shared method descriptor without any security-specific
transport code:

```cpp
ntl::rpc::client client(L"crtsys_ntl_rpc_security_sample");
auto caller = client.invoke(crtsys_ntl_rpc_security::caller_info);
auto value = client.invoke(crtsys_ntl_rpc_security::user_mode_echo, 42u);
```

Endpoint ACLs decide who may open the RPC device. `on_authorized()` adds a
per-method decision before request deserialization and callback execution. A
real driver can replace the short user-mode policy above with
`call_context::check_access()` and its own security descriptor. The exhaustive
allow/deny, impersonation, and callback-suppression cases remain in
[`test/rpc/security`](../../test/rpc/security), keeping this sample readable.

## Async Model

NTL RPC does not implement asynchronous calls with `std::promise` or
`std::future`. `ntl::rpc::async_call<T>` owns the native Windows asynchronous
I/O state required by one request:

- an `OVERLAPPED` `DeviceIoControl` request
- the request and response buffers
- a waitable event
- the device handle needed by `GetOverlappedResult` and `CancelIoEx`

It deliberately offers future-like `wait()`, `wait_for()`, and `get()` methods,
but also provides `cancel()` for the underlying I/O request. Standard
`std::future` has no operation-specific Windows I/O cancellation contract and
would not by itself solve the buffer and `OVERLAPPED` lifetime requirements.

Generated contracts provide matching synchronous and asynchronous convenience
functions. `add(40, 2)` returns the result directly, while
`delayed_add_async(...)` returns an owned `async_call<int>`:

```cpp
auto call = crtsys_ntl_rpc_sample::delayed_add_async(
    std::uint32_t{100}, 40, 2);

// The caller can do other work here.
if (call.wait_for(std::chrono::seconds(2)) ==
    ntl::rpc::async_wait_status::completed) {
  const int result = call.get();
}
```

Both convenience functions open the named endpoint for an independent call.
For a sequence of calls, keep one `ntl::rpc::client` and use the generated
`delayed_add_3_method` descriptor with `client.invoke_async(...)` to reuse the
same connection.

The cancellation example starts a two-second request, observes a 50 ms
timeout, calls `cancel()`, and then verifies that `get()` reports
`ERROR_OPERATION_ABORTED`. The server callback checks its named context so the
kernel work stops promptly instead of merely discarding a late result.

## C++20 Stop Tokens And Coroutines

The C++14-compatible overload remains the baseline. When the app is compiled
as C++20 or later, the same client also accepts a `std::stop_token`:

```cpp
std::stop_source source;
auto call = crtsys_ntl_rpc_sample::delayed_add_async(
    source.get_token(), std::uint32_t{2000}, 40, 2);

source.request_stop(); // Requests CancelIoEx for this call.
```

Including `<ntl/rpc/coroutine>` makes the move-only call awaitable without
changing its native I/O ownership:

```cpp
auto add_with_coroutine(std::stop_token token) -> application_task<int> {
  co_return co_await crtsys_ntl_rpc_sample::delayed_add_async(
      token, std::uint32_t{2000}, 40, 2);
}
```

The C++ standard supplies coroutine handles and suspension rules, but it does
not define a general `task<T>` type. `app/coroutine_task.hpp` is therefore a
small example-only owner for the top-level sample coroutine. The NTL feature is
the `co_await` adapter for `async_call<T>`; applications may use it with their
own coroutine task or scheduler. Completion is observed through a Windows
thread-pool wait, so awaiting does not consume one blocking thread per RPC.

A stop request follows the same transport path as explicit `cancel()`:
`std::stop_token` -> `CancelIoEx` -> server `call_context`. The suspended
coroutine resumes once with `ERROR_OPERATION_ABORTED`. The stop-token overload
and `<ntl/rpc/coroutine>` are C++20-only; existing C++14 and C++17 calls remain
unchanged.

## Loading

Use your normal isolated driver-test VM:

```bat
sc create CrtSysNtlRpcSample binpath= "C:\path\to\crtsys_ntl_rpc_sample.sys" type= kernel start= demand
sc start CrtSysNtlRpcSample
sc stop CrtSysNtlRpcSample
sc delete CrtSysNtlRpcSample
```

## User-Mode RPC App

The sample app runs the synchronous, asynchronous, explicit-cancellation,
coroutine, and stop-token files in sequence:

```bat
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe 21 7
```

Representative output:

```text
sync: add=42 value=21 doubled=42 biased=28 server_irql=0 series=4
async: request started; caller remains available
async: completed with add=42
cancel: kernel callback observed cancellation
coroutine: suspended without blocking the caller
coroutine: resumed with add=42
stop_token: coroutine resumed with cancellation
reliable notification: sequence=1 text=reliable progress 42
stream: sequence=1 text=driver received: chunk 1
stream: sequence=2 text=driver received: chunk 2
stream: sequence=3 text=driver received: chunk 3
stream: completed
stream batch: sequence=1 text=driver received: batched chunk 1
stream batch: sequence=2 text=driver received: batched chunk 2
stream batch: sequence=3 text=driver received: batched chunk 3
all RPC examples completed
```
