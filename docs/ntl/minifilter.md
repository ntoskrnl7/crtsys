# Minifilter Helpers

[Back to NTL documentation](./README.md)

`ntl::flt` is the NTL entry and callback layer for Windows file-system
minifilters. Filter Manager still owns filter registration, instance ordering,
I/O callback dispatch, and teardown. NTL provides typed, non-owning facades and
keeps the callback tables alive for the registered filter.

## Entry And Registration

Define `ntl::flt::main` and move a registration into the driver:

```cpp
#include <ntl/flt/all>

ntl::status ntl::flt::main(ntl::flt::driver& driver,
                           std::wstring_view) {
  ntl::flt::registration callbacks;
  callbacks.on(
      ntl::flt::operation::create,
      [](ntl::flt::create_callback_data, ntl::flt::related_objects,
         void*& completion) noexcept {
        completion = nullptr;
        return ntl::flt::pre_result::success_no_callback;
      });
  return driver.start(std::move(callbacks));
}
```

`ntl::flt::operation` names the Filter Manager operation without exposing raw
`IRP_MJ_*` values. A pre-operation-only registration can pass flags directly:

```cpp
callbacks.on(ntl::flt::operation::write, pre_write,
             ntl::flt::operation_flags::skip_paging_io);
```

No `nullptr` post-operation placeholder is required.
Only `operation::read` and `operation::write` have an overload that accepts
`operation_flags`. Passing read/write-only flags with another operation is a
compile-time error rather than a runtime registration failure.

For CMake, select the minifilter model explicitly:

```cmake
crtsys_add_driver(my_filter MINIFILTER NTL driver/main.cpp)
```

For a Visual Studio WDK project using the NuGet package:

The easiest setup is **Project Properties > Driver Settings > Driver Model**,
then set **crtsys WDM entry point** to **NTL Minifilter**. The package writes the
two properties below through its MSBuild targets:

```xml
<DriverType>WDM</DriverType>
<CrtSysIsMinifilter>true</CrtSysIsMinifilter>
<CrtSysUseNtlFltMain>true</CrtSysUseNtlFltMain>
```

These settings select `CrtSysNtlFltDriverEntry`, initialize the crtsys
runtime, link `fltmgr.lib`, call `ntl::flt::main`, and unregister the filter
before runtime teardown.

## Instances And Altitudes

A registered filter and an attached instance are different lifetime units.
The INF defines one or more named instance configurations, each with an
altitude and attachment flags. Filter Manager then creates a separate
`PFLT_INSTANCE` whenever one configuration is attached to a volume. The same
default definition can therefore produce many runtime instances across
volumes, and a volume can host multiple explicitly selected definitions at
different altitudes.

Altitude is installation metadata, not an argument to `registration::on()` or
`driver::start()`. Define production instances in both the downlevel
`Instances` and Windows 11 24H2 `Parameters\Instances` INF layouts:

```ini
HKR,"Parameters\Instances","DefaultInstance",0x00000000,%DefaultInstance%
HKR,"Parameters\Instances\%DefaultInstance%","Altitude",0x00000000,%DefaultAltitude%
HKR,"Parameters\Instances\%DefaultInstance%","Flags",0x00010001,0
HKR,"Parameters\Instances\%SecondaryInstance%","Altitude",0x00000000,%SecondaryAltitude%
HKR,"Parameters\Instances\%SecondaryInstance%","Flags",0x00010001,1
```

`Flags=0` permits automatic attachment. `Flags=1` suppresses automatic
attachment, allowing the named definition to be selected explicitly. A real
product must use altitudes allocated according to Microsoft's minifilter
altitude policy; the values in the sample are development-only.

Inside callbacks, `objects.instance()` identifies the exact attachment.
`instance_context<T>` consequently stores separate state for every
filter/volume/altitude attachment. Query its stable identity at
`PASSIVE_LEVEL` when diagnostics or policy need it:

```cpp
auto information = objects.instance().try_information();
if (information) {
  // information->name, altitude, volume_name, and filter_name are owning
  // strings and remain valid after the Filter Manager query buffer is freed.
}
```

Kernel code that already owns an `ntl::flt::volume` can manage an explicit
attachment through the filter facade:

```cpp
auto attached = driver.filter().try_attach(volume, L"Product Secondary");
if (!attached)
  return attached.status();

auto identity = attached->view().try_information();
// instance_ref releases the rundown reference; detaching is a separate action.
driver.filter().try_detach(volume, L"Product Secondary");
```

`try_attach()` reads the named definition's attributes from the installed INF.
`try_attach_at()` is available for explicit diagnostic placement, but named INF
definitions are the normal production contract. `try_instances()` enumerates
all current attachments owned by a filter. These helpers use owning STL
strings and vectors, so their NTL contract is `PASSIVE_LEVEL` even where the
underlying Filter Manager query permits `APC_LEVEL`.

Native Filter Manager registrations cannot be detached manually when
`InstanceQueryTeardownCallback` is null. NTL registers an allow callback by
default so `FilterDetach`, `FltDetachVolume`, and `filter::try_detach()` work
without an empty user callback. Use `on_instance_query_teardown()` when the
driver must inspect outstanding per-instance work and possibly return
`STATUS_FLT_DO_NOT_DETACH`. Use `deny_manual_detach()` for an unconditional
deny policy; `allow_manual_detach()` restores the NTL default.

## Callback Facades

`ntl::flt::callback_data<Operation>` wraps `FLT_CALLBACK_DATA` without owning
it. Every operation has a public `<operation>_callback_data` alias, such as
`create_callback_data`, `write_callback_data`, and `cleanup_callback_data`,
which makes the operation part of the callback signature. The facade provides
I/O status, completion, operation-typed parameters, and RAII file-name
queries.
`ntl::flt::related_objects` exposes typed views of the filter, volume,
instance, and kernel `FILE_OBJECT`.

### Generic Lambdas And Editor Completion

The C++ compiler accepts generic callbacks and instantiates their `auto`
parameters from the operation tag:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](auto data, auto objects, auto& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

`test/flt/compile/operation_callback.cpp` keeps this form under compile
coverage. It is not the recommended spelling for application code because
current Visual Studio IntelliSense, the Microsoft C/C++ VS Code extension,
and clangd can infer or diagnose the contextual type but may still return no
member list at `data.` inside the generic lambda. This is an editor/language
server completion limitation, not a failure of NTL callback typing or C++
compilation.

Public samples therefore spell out the operation-specific aliases so member
completion, navigation, and parameter discovery remain available:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects,
       void*& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

Use the operation-specific parameter view instead of selecting a member of the
native `FLT_PARAMETERS` union manually:

```cpp
callbacks.on(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data, ntl::flt::related_objects,
       void*&) noexcept {
      const auto write = data.parameters();

      const ULONG length = write.length();
      const LARGE_INTEGER offset = write.byte_offset();
      (void)length;
      (void)offset;
      return ntl::flt::pre_result::success_no_callback;
    });
```

The operation passed to `registration::on()` determines the callback-data C++
type. A create registration accepts `create_callback_data`, whose
`parameters()` returns only `create_parameters`; a write registration accepts
`write_callback_data`, whose `parameters()` returns only `write_parameters`.
An operation/callback-data mismatch is rejected while resolving the `on()`
overload, so IDE semantic analysis can report the error at the registration
call instead of waiting for the callback body or runtime registration.
The generic spelling `callback_data<operation::close>` remains available when
code generation or generic registration logic needs it.
Code cannot ask create callback data for read or write parameters. Parameter
setters call `FltSetCallbackDataDirty()` automatically. `native_iopb()` remains
available only as an explicit escape hatch for specialized Filter Manager
operations.

`name_information` owns the reference returned by
`FltGetFileNameInformation`. Call `try_parse()` before reading parsed path
components. Destruction calls `FltReleaseFileNameInformation`. Do not call
`FltUnregisterFilter` yourself after `driver.start()` succeeds; the NTL entry
layer owns unregister and crtsys runtime teardown.

## Normal-Only Post Callbacks And Completion State

A post callback without `void* CompletionContext` and
`post_operation_flags` declares that it contains normal I/O completion work:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects) noexcept {
      // Called only after normal completion, never while the instance drains.
      if (data.io_status().is_ok())
        record_success(objects);
    });
```

NTL returns `FLT_POSTOP_FINISHED_PROCESSING` after this callback. It skips the
callback when Filter Manager supplies `FLTFL_POST_OPERATION_DRAINING`. This
form is appropriate when pre-operation does not acquire anything that must be
released by post-operation.

Use `on_with_completion<T>()` when pre-operation must carry owned per-I/O state
to post-operation:

```cpp
struct request_state {
  std::uint32_t original_length;

  explicit request_state(std::uint32_t length) noexcept
      : original_length(length) {}
  ~request_state() noexcept = default;
};

callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_slot<request_state>& completion) noexcept {
      if (completion.try_emplace(data.parameters().length()).is_err())
        return ntl::flt::pre_result::success_no_callback;
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> completion) noexcept {
      if (completion && data.io_status().is_ok())
        record_bytes(completion->original_length);
    });
```

`completion_slot<T>` allocates `T` from `NonPagedPoolNx`. The object transfers
to Filter Manager only when pre-operation returns
`success_with_callback` or `synchronize`; all other pre-operation results cause
immediate destruction. `completion_ref<T>` is a non-owning view valid only for
the current post callback.

NTL owns and destroys the object in every path:

| Path | User post callback | Destruction |
| --- | --- | --- |
| Pre does not request post | Not called | Before pre-operation returns |
| Normal completion | Called | Immediately after the post callback |
| Normal completion requests WhenSafe | Immediate and safe callbacks called | Immediately after the safe callback |
| WhenSafe cannot be scheduled | Immediate callback only | Before NTL resumes completion |
| Instance draining, flags-less typed post | Skipped | In the draining trampoline |
| Instance draining, flags-aware typed post | Called with `flags.draining()` | Immediately after the callback |

The completion-state destructor is the right place to release resources owned
solely by that I/O, such as pool memory, context references, object references,
rundown protection, and outstanding-I/O counters. It is not a replacement for
normal completion logic: do not inspect final I/O data, update success metrics,
or start new work from the destructor.

`T` must be nothrow-constructible for the arguments passed to `try_emplace()`
and nothrow-destructible. Its destructor and every RAII member must be legal at
the post callback's possible IRQL, including draining. Keep the state
nonpageable and nonblocking. Use the flags-aware low-level callback when the
driver must observe draining explicitly, return native post results, pend
completion, or coordinate ownership that cannot be represented by the typed
state.

## Post And When-Safe Processing

Use the same `on()` API when a post-operation path has both an IRQL-independent
part and work that requires `IRQL <= APC_LEVEL`:

```cpp
callbacks.on(
    ntl::flt::operation::read,
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects objects) noexcept {
      // A: work that is always legal in the native post callback.
      completed.fetch_add(1, std::memory_order_relaxed);

      // B: finish APC-safe work here when possible. Request WhenSafe only
      // when the current IRQL or another runtime condition requires it.
      if (ntl::is_irql_at_most(ntl::irql::apc)) {
        inspect_contexts(objects);
        return ntl::flt::post_continuation::finished;
      }
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects objects) noexcept {
      // NTL dispatches this through FltDoCompletionProcessingWhenSafe.
      // This callback runs at IRQL <= APC_LEVEL.
      inspect_contexts(objects);
    });
```

The immediate post callback chooses the path for each completion. Return
`post_continuation::finished` after doing all required work or when no safe work
is needed. Return `post_continuation::when_safe` to make NTL call
`FltDoCompletionProcessingWhenSafe`. This supports the usual A-then-conditional
B pattern without forcing every completion through the safe callback.
The user callbacks do not need `post_operation_flags`: NTL sees the native flags
in its trampoline and skips both callbacks while the instance drains.

Typed completion state can follow the same path:

```cpp
callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    pre_write,
    [](ntl::flt::write_callback_data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_nonpageable_fields(state);
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::write_callback_data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_pageable_fields(state);
    });
```

The same `completion_ref<T>` remains valid through the safe callback. NTL
destroys it after the immediate callback returns `finished`, after the safe
callback returns, during draining, or when Filter Manager cannot invoke or
queue the safe callback.

The flags-aware low-level overload remains available when the driver must see
`FLT_POST_OPERATION_FLAGS`, manage a native `void* CompletionContext`, or
return a native post result. In that form the driver must handle draining
itself: release only completion state, skip normal completion work, and never
request WhenSafe while `flags.draining()` is true.

For read and write registrations with a safe callback NTL automatically sets
`skip_paging_io`, because paging I/O cannot be posted this way. When the
immediate callback requests WhenSafe for a non-IRP operation, or Filter Manager
cannot queue the work, the safe callback is not called and NTL finishes the
operation. The safe callback returns `void`, so it cannot accidentally pend
completion again. Mandatory work must therefore be completed in the immediate
callback whenever it is already legal there; use the safe callback for work
that is valid to omit when Filter Manager cannot provide a safe continuation.

## Typed File-System Contexts

Declare each state type once, register the declaration, and use the same
declaration to retrieve or create state from a callback:

```cpp
struct file_state {
  std::atomic<std::uint32_t> writes{0};

  file_state() noexcept = default;
  ~file_state() noexcept = default;
};

inline constexpr ntl::flt::file_context<file_state> file_state_context{};

ntl::flt::registration callbacks;
callbacks.context(file_state_context);

// Use this after a successful create, or in another legal <= APC_LEVEL path.
auto state = objects.try_get_or_create(file_state_context);
if (state)
  (*state)->writes.fetch_add(1, std::memory_order_relaxed);
```

The public declarations describe the lifetime unit:

| Declaration | State belongs to |
| --- | --- |
| `volume_context<T>` | one volume |
| `instance_context<T>` | one minifilter instance attached to a volume |
| `file_context<T>` | one file within an instance, shared across its streams |
| `stream_context<T>` | one file stream within an instance |
| `stream_handle_context<T>` | one open `FILE_OBJECT` for a stream |
| `transaction_context<T>` | one file-system transaction within an instance |

`try_get<T>()` is exposed as `objects.try_get(declaration)`.
`try_get_or_create()` performs the complete `FltGet*Context` /
`FltAllocateContext` / `FltSet*Context(KEEP_IF_EXISTS)` sequence. When two
threads race to initialize the same object, it returns the winner's referenced
context and destroys the losing allocation. `context_ref<T>` is move-only and
calls `FltReleaseContext` automatically. Filter Manager invokes the registered
cleanup callback when the final reference disappears; that callback runs the
C++ context destructor.

Context constructors and destructors must be `noexcept`, and over-aligned or
larger-than-`MAXUSHORT` state is rejected at compile time. A `volume_context`
defaults to `context_pool::nonpaged` (`NonPagedPool`), which is the only pool
Filter Manager permits for that scope. Its public declaration accepts no pool
argument, so selecting `PagedPool` or `NonPagedPoolNx` is a compile-time error;
only its diagnostic pool tag is configurable. Every other scope defaults to
`context_pool::nonpaged_nx` (`NonPagedPoolNx`) and may explicitly select
`nonpaged`, `nonpaged_nx`, or `paged`. NTL permits one C++ state declaration per
context scope because Filter Manager attaches at most one context of that type
for each filter instance/object relationship.

### Cache Names At Create Time

Do not make a new name query in every read, write, cleanup, or close callback.
Successful post-create callbacks run at `PASSIVE_LEVEL`, so they are a natural
place to query the final normalized name once and retain it in typed contexts:

```cpp
auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                FLT_FILE_NAME_QUERY_DEFAULT);
if (name && name->try_parse().is_ok()) {
  auto file_name = name->try_reference();
  if (file_name) {
    (void)objects.try_get_or_create(file_state_context,
                                    std::move(*file_name));
  }
  (void)objects.try_get_or_create(stream_state_context, std::move(*name));
}
```

`name_information::try_reference()` calls
`FltReferenceFileNameInformation`, so each context owns an independent RAII
reference. A file context is shared across alternate streams; use its parsed
file components such as `parent_directory()` and `final_component()`. Keep the
complete normalized name, including any alternate-stream component, in the
stream context. Later callbacks can retrieve these contexts without issuing a
new name query. This is especially useful in read, write, pre-cleanup, and
pre-close paths. A fresh `FltGetFileNameInformation` request after cleanup may
fail with `STATUS_FLT_INVALID_NAME_REQUEST`, while the retained reference
remains available until its context is destroyed.

The retained information is a snapshot, not a rename subscription. A filter
that tracks renames or hard links must update or invalidate its context in the
corresponding set-information paths. The underlying Filter Manager name
information is allocated from paged pool, so dereference it only at
`IRQL <= APC_LEVEL`; retaining it avoids a query but does not make paged memory
legal at `DISPATCH_LEVEL`.

Context access follows Filter Manager's native restrictions:

- allocation, get, set, and support queries require `IRQL <= APC_LEVEL`;
- file, stream, and stream-handle contexts cannot be queried or set in
  pre-create or post-close callbacks;
- paging files and some third-party file systems may not support these
  contexts; `supports()` and `try_get_or_create()` preserve that result;
- `file_context` uses `FltSupportsFileContextsEx` so Filter Manager's
  single-stream file-system compatibility path remains available.

See Microsoft's [minifilter context management](https://learn.microsoft.com/windows-hardware/drivers/ifs/managing-contexts-in-a-minifilter-driver)
and [file-system context support](https://learn.microsoft.com/windows-hardware/drivers/ifs/file-system-support-for-contexts)
for the underlying object and file-system contracts.

## Typed Communication Ports

`ntl::flt::communication_server` carries the same `ntl::rpc::method`
descriptors used by an IOCTL RPC endpoint over a Filter Manager communication
port. The descriptor defines the stable method ID, argument types, response
type, serialized request limit, and decode-allocation limit; the transport is
still `FltCreateCommunicationPort` and `FilterSendMessage`.

Declare the contract once in a header shared by the driver and app. The kernel
expansion registers the callback; the user-mode expansion generates
`connect()`, `query_count()`, and `query_count_async()`:

```cpp
NTL_FLT_RPC_BEGIN(product_messages, L"\\ProductPort")

NTL_FLT_ADD_CALLBACK_ID(
    product_messages, 0xA50, std::uint32_t(std::uint32_t), query_count,
    [](std::uint32_t base) noexcept { return base + 1; })

NTL_FLT_RPC_END(product_messages)
```

Register the generated server before starting the filter:

```cpp
auto messages = product_messages::make_server();
auto status = driver.add_communication_port(product_messages::port_name,
                                             std::move(messages));
if (status.is_err())
  return status;

return driver.start(std::move(callbacks));
```

The user-mode client uses the same descriptor:

```cpp
auto client = product_messages::connect();
const auto answer = product_messages::query_count(client, std::uint32_t{41});
```

The explicit client argument is intentional: calls, shared regions, and future
connection state all stay on the same Filter Manager connection instead of
silently reconnecting for each generated function.

### Async, Cancellation, And Coroutines

`FilterSendMessage` itself is synchronous. NTL's async method therefore uses
it only to submit a bounded request. The minifilter executes the callback on a
PASSIVE_LEVEL worker and sends the result through `FltSendMessage`; a bounded
set of overlapped `FilterGetMessage` receivers dispatches completions by
fixed-width request ID. Multiple calls can be outstanding on one client, while
`communication_port_options::max_pending_async()` bounds pending work and
copied request memory independently for each connection (64 by default):

```cpp
auto first = product_messages::query_count_async(client, 40);
auto second = product_messages::query_count_async(client, 41);

const auto first_result = first.get();
const auto second_result = second.get();
```

`ready()`, `wait_for()`, `wait()`, `cancel()`, and `get()` follow the same
one-result ownership model as `ntl::rpc::async_call`. Cancellation is
cooperative: `cancel()` marks the kernel request and a long-running callback
checks `communication_context::cancellation_requested()`. Completion can win
the race with cancellation, so callbacks must not assume that a cancellation
request undoes work already committed.

In C++20, include `<ntl/flt/coroutine>` and move the generated async call into
`co_await`:

```cpp
task<std::uint32_t> query(ntl::flt::communication_client& client) {
  co_return co_await product_messages::query_count_async(client, 41);
}
```

The generated `_async` function also accepts `std::stop_token` in C++20. The
token requests the same cooperative cancellation. C++14 and later retain the
ordinary synchronous and explicit async-call APIs.

The driver can host multiple named ports. Each connection has independent
pinned-memory quotas and method dispatch. NTL closes listeners before
`FltUnregisterFilter`; Filter Manager then invokes the disconnect callback for
every remaining client before the server state is destroyed.

### Contract, Sessions, Notifications, And Streams

Use `NTL_FLT_RPC_BEGIN_CONTRACT` when the app and minifilter can be updated
independently. `connect()` queries the running endpoint and rejects a different
application version or missing capability bits before the first method call:

```cpp
NTL_FLT_RPC_BEGIN_CONTRACT(product_messages, L"\\ProductPort", 1, 0x5ull)
```

The contract query also advertises every app-to-driver method,
driver-to-app method, notification, and stream with fixed-width request,
response, decode-allocation, batch, and priority limits. Apps can fail before
normal traffic begins when a shared descriptor does not match the running
driver:

```cpp
auto contract = client.query_contract();
client.require_method(product_messages::query_count_method);
client.require_client_method(product_messages::app_transform_method);
client.require_notification(product_messages::progress_notification);
client.require_stream(product_messages::numbers_stream);
```

The explicit application contract version is the schema identity. NTL does
not hash compiler type names because those strings are not stable across x86,
x64, or MSVC toolsets. Change the contract version when a serialized type or
method signature changes incompatibly.

### Connections And Driver-To-App Requests

`communication_server::on_connect()` can accept or reject a client before the
port becomes usable and can attach typed application state. `on_disconnect()`
observes the committed disconnect. A copied `communication_connection` stays
safe to inspect after disconnect: `connected()` becomes false and targeted
operations fail instead of dereferencing the old Filter Manager port.

```cpp
struct client_state {
  std::uint32_t accepted_calls = 0;
};

server
    .on_connect([](ntl::flt::communication_connection& connection) {
      connection.state(std::make_shared<client_state>());
      return ntl::status::ok();
    })
    .on_disconnect([](ntl::flt::communication_connection& connection) {
      connection.clear_state();
    });
```

The same port is bidirectional. Register a driver-originated method before the
server starts, register its handler in the app, and invoke it through the
connection associated with the current callback:

```cpp
// Driver setup and callback path.
server.register_client_method(product_messages::app_transform_method);
auto result = publisher.try_request(
    context.connection(), product_messages::app_transform_method,
    std::chrono::seconds(2), value);

// App setup before it invokes the driver path that makes the request.
client.on_request(product_messages::app_transform_method,
                  [](std::uint32_t value) noexcept { return value * 3; });
```

The app keeps four overlapped `FilterGetMessage` receives pending. One receive
path dispatches asynchronous method completions, notifications, stream
records, and driver-originated requests. App request handlers can therefore
run concurrently and must protect shared state accordingly.

Every connection opens one server session. A transient notification is
best-effort and is consumed without an ACK:

```cpp
auto wait = product_messages::progress_async(client);
// The driver calls publisher.try_notify(progress_notification, event).
auto event = wait.get();
```

`publisher.try_notify(notification, payload)` broadcasts to current
subscribers. `publisher.try_notify(connection, notification, payload)` targets
one connected subscriber. Targeted delivery returns `STATUS_NOT_FOUND` when
the connection has disconnected or has not subscribed to that channel; it
does not silently become a broadcast.

A reliable notification remains in the session's bounded queue until the app
acknowledges its sequence. `max_reliable_records()` and
`max_reliable_bytes()` apply independently to each session, so a stalled app
cannot grow kernel memory without bound:

```cpp
auto delivery = product_messages::progress_reliable(client);
process(delivery.payload());
client.acknowledge(product_messages::progress_notification, delivery);
```

Normal handle or process termination removes its session. To reconnect without
losing unacknowledged reliable records, explicitly detach it first. This
invalidates the current client, cancels its pending operations, and returns the
token used by `resume()`:

```cpp
auto token = client.preserve_session();
auto resumed = product_messages::resume(token);
auto replayed = product_messages::progress_reliable(resumed);
resumed.acknowledge(product_messages::progress_notification, replayed);
```

Reliable queues live only while the minifilter remains loaded. They are not a
disk-backed event log. A product that needs persistence across unload or reboot
can install `communication_notification_store`. NTL calls `persist()` before a
record becomes visible, removes it after an explicit ACK, and calls `restore()`
when an in-memory session token is absent. The hook is the policy boundary:
NTL itself performs no file or registry I/O and provides no built-in disk
format.

Storage hooks run at `PASSIVE_LEVEL`, without NTL connection or session locks,
and must be thread-safe. The `communication_record_view::data` range is valid
only for the hook call. A store used with batch delivery must override
`acknowledge_batch()` as an atomic operation; NTL deliberately does not emulate
a batch by making several externally visible single-record commits. Explicitly
closing a session invokes `erase_session()`.

Reliable receive can request bounded batches:

```cpp
auto wait = client.receive_reliable_batch_async(
    product_messages::progress_notification);
auto batch = wait.get();
consume(batch.values());
client.acknowledge(product_messages::progress_notification, batch);
```

`max_reliable_records()` and `max_reliable_bytes()` bound each session.
`reliable_overflow(reject_newest)` preserves queued data and rejects the new
record. `reliable_overflow(drop_oldest)` evicts the oldest record that is not
being delivered. The latter is intentionally unavailable when an external
store is installed because NTL cannot assume that an application store treats
eviction as an ACK.

A typed stream combines one bounded app-to-driver upload method with one
ordered, ACK-based driver-to-app channel. Upload batching serializes a bounded
vector in one request; every download record is retained until acknowledged:

```cpp
auto stream = product_messages::numbers(client);
stream.write_batch(std::vector<std::uint32_t>{10, 20});

for (int index = 0; index != 2; ++index) {
  auto reply = stream.read();
  consume(reply.payload());
  stream.acknowledge(reply);
}
stream.close();
```

Use `read_batch()` or `read_batch_async()` to receive up to the descriptor's
bounded batch size in one Filter Manager message and acknowledge that batch in
one transport operation. A batch may contain one record; callers must not
assume that concurrent writes are always coalesced.

The driver callback receives `communication_stream_context<Stream>`. Its
`try_write()`, `try_complete()`, and `try_fail()` methods enqueue data or a
terminal record into the same bounded reliable queue. Cross-channel priority
selects which queued channel is delivered next; records within one stream keep
their sequence and ACK contract.

Notification waits, reliable waits, stream writes, and stream reads expose
synchronous and asynchronous forms. In C++20 their asynchronous values can be
`co_await`ed and can be bound to `std::stop_token`; C++14 and later retain the
explicit wait/cancel/get API.

### Authorization And Resource Limits

`communication_port_options` bounds connections, pending async calls,
retained sessions, reliable records/bytes, and pinned regions. Set a custom
Filter Manager port security descriptor when the default ACL is not the
product policy. For method-specific policy, `on_authorized()` and
`NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID` run before request deserialization:

```cpp
NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID(
    product_messages, 0xA20, reply_type(request_type), privileged_call,
    [](const ntl::flt::communication_context& context) noexcept {
      return authorize_process(context.requestor_process_id());
    },
    [](request_type request) { return handle(request); })
```

The policy returns `NTSTATUS` or `ntl::status`. It can use the captured original
requestor process ID and `session_id()` with normal Windows kernel security
APIs. Authorization happens before serializer allocation, so a rejected caller
cannot force the protected method to decode an attacker-controlled container.

### Shared Regions And Cross-Bitness

Large or frequently exchanged fixed-layout data can use a connection-bound
shared region instead of serializing every record. The app allocates and
registers the region, then sends `ntl::ipc::buffer_token` values through a
normal typed method:

```cpp
auto region = client.register_shared_region(bytes);
auto input = region.token(0, input_bytes);
auto output = region.token(output_offset, output_bytes);
client.invoke(process_buffers, input, output);
```

The kernel callback resolves each token with the required access:

```cpp
messages.on(process_buffers,
    [](const ntl::flt::communication_context& context,
       ntl::ipc::buffer_token input,
       ntl::ipc::buffer_token output) {
      auto readable = context.try_resolve(
          input, ntl::ipc::region_access::driver_read);
      auto writable = context.try_resolve(
          output, ntl::ipc::region_access::driver_write);
      // Attach an ntl::ipc::shared_ring or validate another fixed layout.
    });
```

Tokens contain fixed-width region identity, offset, and length fields, never a
process pointer. The same protocol therefore supports an x86 app connected to
an x64 minifilter. Resolution checks connection ownership, generation, bounds,
and access before exposing the pinned range. Destroy or `close()` the
`registered_port_region` only after all sync or async calls using its tokens
have completed;
unregistration releases the MDL before the app frees the virtual allocation.

Message callbacks run at `PASSIVE_LEVEL`. NTL copies Filter Manager's
unaligned user buffers under structured exception handling before decoding,
enforces the method's request/allocation limits, and rejects malformed framing
or stale region tokens without tearing down a healthy connection. The runtime
fixture tests typed calls, concurrent async completion dispatch, cooperative
cancellation, shared rings, malformed and oversized framing, invalid
shared-region range/access/quota tokens, stale-token rejection, and connection
reuse after rejected requests. Its advanced communication tests also cover
typed connection state, connect/disconnect observation, on-connect rejection,
connection/session quotas, driver-to-app requests, targeted-send subscription
checks, detailed contract mismatch, reliable record/byte quotas, reliable
batch ACK and duplicate rejection, external restore hooks, drop-oldest
overflow, stream failure completion, C++20 coroutine calls, and concurrent
connect/call/close stress. These test-only paths stay out of the onboarding
sample.

## IRQL And Lifetime

The callback's native Filter Manager contract always wins:

- Driver entry, registration, instance setup, and unload are
  `PASSIVE_LEVEL` paths.
- Some pre-operation callbacks can run at `APC_LEVEL`.
- A post-operation callback can run at `DISPATCH_LEVEL`. Keep pageable code,
  blocking operations, exceptions, and general CRT/STL work out of that path.
- `try_query_name()` does not make a name query legal. Call it only on an I/O
  path where `FltGetFileNameInformation` is permitted.
- Facades are non-owning unless their documentation explicitly says RAII.
  Do not retain callback data or related-object views after the callback.

The buildable [minifilter sample](../../examples/minifilter-ntl-driver) keeps
the onboarding path focused on typed create/read/write/cleanup callbacks and a
cached stream-name context. Its flags-less post-create callback demonstrates
normal-completion-only work without exposing draining state. The standalone
[runtime fixture](../../test/flt/runtime) covers file, stream, stream-handle,
and instance contexts, typed RAII completion-state lifetime, conditional
WhenSafe processing, and automatic plus manual instances at two development
altitudes.

Microsoft references:

- [Creating an INF file for a minifilter driver](https://learn.microsoft.com/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver)
- [Load order groups and altitudes](https://learn.microsoft.com/windows-hardware/drivers/ifs/load-order-groups-and-altitudes-for-minifilter-drivers)
- [FltAttachVolume](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltattachvolume)
