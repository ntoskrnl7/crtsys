# Windows Filtering Platform helpers

[Back to NTL documentation](./README.md)

`ntl::wfp` is a typed boundary around the WFP callout and policy-management
lifecycles. It does not hide WFP layers or turn packet processing into a
general firewall DSL. Its purpose is to make the native combinations that
cause the most serious correctness bugs unavailable to ordinary driver code.

The kernel and controller surfaces are deliberately separate:

- `<ntl/wfp/callout>`, `<ntl/wfp/classify>`, `<ntl/wfp/flow>`,
  `<ntl/wfp/packet>`, `<ntl/wfp/stream>`, and `<ntl/wfp/injection>` are
  kernel-only;
- `<ntl/wfp/connect_redirect>` selects the kernel redirect-handle surface or
  the user-mode accepted-socket handoff for the current build mode;
- `<ntl/wfp/management>` is user-only; and
- `<ntl/wfp/all>` selects the correct side.

The build integration targets Windows 8 or later and links `fwpkclnt.lib` when
WFP is selected. Visual Studio/NuGet consumers select **NTL WFP** on the
**crtsys WDM entry point** property page, or set the equivalent MSBuild
property:

```xml
<CrtSysWdmEntryPoint>NtlWfp</CrtSysWdmEntryPoint>
```

CMake consumers use:

```cmake
crtsys_add_driver(my_callout WFP NTL src/main.cpp)
```

## Rules enforced by the API

### Layers and keys never become raw GUIDs

Provider, sublayer, callout, and filter keys are distinct types. Callout and
filter keys also contain their layer type:

```cpp
using layer = ntl::wfp::layers::ale_auth_connect_v4;

constexpr GUID native_callout = /* project-owned stable GUID */;
constexpr ntl::wfp::callout_key<layer> callout(native_callout);
```

A key for `stream_v4` cannot be passed to an `ale_auth_connect_v4`
registration or filter. Native GUID access is private and is available only to
the registration and policy writers.

### A classify callback returns a decision

Ordinary classify callbacks receive a non-copyable, callback-scoped,
read-only `classify_event<Layer>` and return `decision`:

```cpp
constexpr auto authorize =
    +[](const ntl::wfp::classify_event<layer>& event) noexcept {
      const auto port =
          event.value(layer::field::remote_port).uint16();
      return port && *port == 443
                 ? ntl::wfp::decision::block
                 : ntl::wfp::decision::continue_classification;
    };
```

The callback cannot access `FWPS_CLASSIFY_OUT0`. The trampoline applies
`FWPS_RIGHT_ACTION_WRITE`, clear-action-right, veto, and absorb semantics.
It also normalizes the result to the native filter action: inspection always
continues, terminating accepts only permit/block, and unknown accepts the
full decision set. This remains enforced when policy was installed outside
`ntl::wfp`. Callbacks must be `noexcept` and return exactly `decision`.

The supported decision layers are:

- `ale_connect_redirect_v4` and `ale_connect_redirect_v6`;
- `ale_auth_connect_v4` and `ale_auth_connect_v6`;
- `ale_auth_recv_accept_v4` and `ale_auth_recv_accept_v6`;
- `ale_flow_established_v4` and `ale_flow_established_v6`;
- `datagram_data_v4` and `datagram_data_v6`; and
- inbound/outbound transport v4 and v6.

### Connect redirection has one legal mutation path

`ALE_CONNECT_REDIRECT` needs writable layer data, but ordinary callbacks still
cannot access `FWPS_CONNECT_REQUEST0` or `FWPS_CLASSIFY_OUT0`. A driver creates
one cached redirect handle and delegates the whole operation:

```cpp
auto redirector =
    ntl::wfp::connect_redirector::try_create(provider_key);

return redirector->redirect(
    event, ntl::wfp::local_proxy_target{proxy_pid, proxy_port});
```

The operation detects connections already redirected by the same handle,
pairs each successful writable-data acquisition with one
`FwpsApplyModifiedLayerData0`, transfers the original endpoints as WFP-owned
context, and blocks on failure. The policy side must use
`connect_redirect_filter_builder`; its constructor requires the proxy PID and
host-order port and fixes the terminating action and raw-context encoding.

The accepting proxy captures the matching handoff and opens its outbound leg:

```cpp
auto handoff =
    ntl::wfp::redirected_connection::capture(accepted_socket);
SOCKET outbound = handoff.connect_original();
```

`connect_original()` attaches WFP's opaque redirect records before connecting
to the original destination. This preserves connection attribution and lets
the kernel loop check permit the proxy's outbound connection. The API is a
TCP byte-stream proxy boundary; it does not decrypt TLS or define an
application-message codec. A user-mode proxy may compose that handoff with
`<ntl/net/tls/stream>` to terminate TLS on the accepted leg and protect the
outbound leg; the WFP callout still never sees TLS plaintext or keys.

### Stream callbacks have a different result type

Stream classification has legal output combinations that do not map to an
ordinary permit/block decision. `add_stream()` therefore accepts only a
callback returning `stream_result`:

```cpp
struct flow_state {
  std::uint64_t inspected = 0;
  ~flow_state() noexcept = default;
};

constexpr auto inspect =
    +[](const ntl::wfp::stream_event<
           ntl::wfp::layers::stream_v4, flow_state>& event) noexcept {
      if (!event.context())
        return ntl::wfp::stream_result::block(event.data().size());

      event.context()->inspected += event.data().size();
      return ntl::wfp::stream_result::permit(event.data().size());
    };
```

The factories are `permit(bytes)`, `block(bytes)`, `need_more(minimum)`,
`defer()`, `drop_connection()`, and `allow_connection()`. The adapter binds
the classify action, stream action, required/enforced counts, and action-right
check. Enforced byte counts are clamped to the indicated stream length.
It prevents inspection filters from becoming editors, converts repeated
need-more at WFP's buffer-limit or no-more-data states to a fail-closed
full-buffer block, rejects outbound defer, and emits drop-connection only for
an unknown-action native filter.

`stream_data_view::bytes()` is an allocation-free `scatter_view` over the
native NBL/MDL fragments. `copy_to()` remains the convenience flattening path.
`cloned_stream_data` is the move-only out-of-band path and always uses
`FwpsDiscardClonedStreamData0`; it cannot be confused with a normal NBL clone.

### Flow contexts transfer ownership once

`callout_driver::add_stream<Context, Callback>()` and
`add_flow_context<Context, Callback>()` return a
`flow_target<Layer, Context>`. A target accepts only the same layer and context
type:

```cpp
auto state = std::make_unique<flow_state>();
const ntl::status status =
    stream_target.associate(flow_handle, std::move(state));
```

On success, ownership transfers to an internal registration holder and WFP
receives only that holder's opaque identity. The registration tracks every
holder; its flow-delete trampoline deletes the context exactly once. On
failure, the context is deleted by the failed transfer path. Context storage
and its destructor must be safe at the IRQL at which the target layer deletes
flows; the destructor is required to be `noexcept`.

Datagram flow contexts use a callback of the form:

```cpp
decision callback(const classify_event<datagram_data_v4>&,
                  proxy_context*) noexcept;
```

The raw integer flow context is not cast by application code.

### Deferred packets have explicit ownership

- `borrowed_packet` is valid only for the callback.
- `referenced_packet` holds a WFP NBL reference.
- `cloned_packet` owns an NBL created by
  `FwpsAllocateCloneNetBufferList0`.
- `cloned_stream_data` owns a chain created by `FwpsCloneStreamData0`.
- `pended_operation` completes an ALE operation from its destructor unless it
  has already been completed.

`network_injector`, `transport_injector`, and `stream_injector` own their
injection handles. Successful asynchronous injection transfers the clone to a
completion context. Injector teardown acquires rundown, waits at
`PASSIVE_LEVEL` for every completion, and only then destroys the native
handle.

A `stream_event` can issue a typed `stream_injection_site<Layer>`. It captures
the WFP-provided flow, callout, layer, and stream flags without exposing
constructors for those IDs. The site is also the capability for
`continue_deferred()`.

## Fragmented bytes without manual MDL walking

`<ntl/net/buffer/scatter_view>` is the common, native-neutral byte layer. It deliberately
contains no WFP or NDIS callback type:

- `scatter_view` borrows read-only fragments and never extends their lifetime;
- `mutable_scatter_view` is returned only by an owning or explicitly mutable
  adapter;
- `byte_cursor` performs bounded sequential and big-endian reads;
- `scan_bytes()` finds a fixed token across fragment boundaries without
  flattening; and
- `owned_bytes` is a move-only nonpaged deep copy with a mandatory
  `buffer_limits` check.

Packet wrappers expose the distinction directly:

```cpp
const auto borrowed = event.packet();
ntl::net::byte_cursor header(borrowed.bytes()); // callback lifetime only

auto saved = borrowed.try_copy(ntl::net::buffer_limits{64 * 1024});
if (!saved)
  return ntl::wfp::decision::block_and_absorb;
```

`borrowed_packet::bytes()` and `stream_data_view::bytes()` enumerate every
`NET_BUFFER_LIST`, `NET_BUFFER`, and MDL fragment. A UDP port field may cross
an MDL boundary; `cloned_packet::rewrite_udp_destination_port()` still edits
it and clears the checksum. No `NdisGetDataBuffer()` contiguity assumption is
required.

The view's provider and backing storage must remain valid for the whole
operation. A view obtained from a classify event must not be kept after the
callback. Copy it to `owned_bytes` before queueing work or suspending a
coroutine. `append_received_data()` performs that copy synchronously.

The same `scatter_view`, cursor, and parser code can be reused by a future
`ntl::ndis` adapter. Native WFP and NDIS callback/lifetime wrappers remain
separate because their retain, clone, return, pause, and injection rules are
not interchangeable.

## Bounded coroutine observation

`<ntl/net/io/async_byte_stream>` provides a fixed-capacity, single-reader ring for
protocol code that is clearer as sequential reads:

```cpp
auto header = co_await stream.read_exactly<message_header>(
    {std::chrono::milliseconds(250)});
if (!header)
  co_return;

const auto body_size = decode_size(*header);
auto body = ntl::net::owned_bytes::try_allocate(
    body_size, ntl::net::buffer_limits{4096});
if (!body)
  co_return;

const ntl::status read =
    co_await stream.read_exactly(body->span());
```

Producers call `append_received_data(scatter_view)`. The call copies fragments
into nonpaged storage before returning, rejects capacity overflow, and never
retains a borrowed native pointer. Awaited continuations resume through a
system work queue at `PASSIVE_LEVEL`. EOF, cancellation, timeout, capacity
overflow, and a competing reader are reported as `NTSTATUS`; two work slots
prevent a second completed read from reusing a work item that is still
resuming the first.

The initial surface intentionally does not define a general `task<T>`:
coroutine-frame ownership belongs to the driver's task/flow lifetime. The
destination span and frame must survive suspension. At teardown, call
`cancel_and_wait()` at `PASSIVE_LEVEL`, then wait for and destroy the owning
task. Destroying a task while its continuation is queued is invalid.

`ntl::wfp::stream_reader` automatically copies a `stream_event` into this
bounded reader and closes it on disconnect, abort, or no-more-data. It is an
asynchronous observation adapter only. It does not defer or retain the
original WFP bytes, so a coroutine cannot later block bytes already permitted
by the classify callback. A verdict-bearing asynchronous WFP path must use
`stream_result::defer()`, `cloned_stream_data`,
`stream_injection_site`, and `stream_injector`; its timeout and memory-limit
failure policy must explicitly choose fail-open or fail-closed.

The controller side has a separate user-mode primitive:
`<ntl/net/io/async_socket>`. It uses overlapped Winsock plus IOCP and offers
`co_await read_some()`, `read_exactly()`, and `write_all()`. It does not share
kernel pool, IRQL, or callback-lifetime machinery with `async_byte_stream`.
`<ntl/net/io/async_framed_stream>` adds a bounded caller-selected message framer,
retains partial and over-read TCP bytes, and yields only complete owning
messages. Length-prefix, delimiter, custom framing, and decoder adapters are
documented in [Content inspection and framing](./inspection.md).
`<ntl/net/tls/stream>` provides a separate Schannel client/server transport; see
[User-mode Schannel TLS streams](./tls-stream.md).
The stream-edit controller uses these awaiters for its real loopback traffic;
see [User-mode coroutine sockets](./async-socket.md) for buffer, task,
cancellation, and completion-context lifetime rules.

## Transactional user-mode policy

`dynamic_session` always opens a dynamic WFP session. Policy is installed only
inside `install()`:

```cpp
ntl::wfp::dynamic_session session;
session.install([](ntl::wfp::policy_transaction& tx) {
  const auto provider = tx.add_provider(/* provider_spec */);
  const auto sublayer = tx.add_sublayer(provider, /* sublayer_spec */);
  const auto callout =
      tx.add_callout<layer>(provider, /* callout_spec<layer> */);

  ntl::wfp::filter_builder<layer> filter(
      /* filter_key<layer> */, L"Block selected TCP port");
  filter.protocol_equal(IPPROTO_TCP).remote_port_equal(443);
  tx.add_filter(sublayer, callout, filter);
});
```

Normal return commits. Any exception aborts. Provider, sublayer, and callout
references are transaction-scoped capabilities with a generation identity;
mixing references from two transactions or providers throws before a native
filter call.

The four policy builders fix their native action:

| Builder | Layer/use | Native action |
| --- | --- | --- |
| `filter_builder<Layer>` | ALE authorization | terminating + clear action right |
| `inspection_filter_builder<Layer>` | flow/packet/monitor-only stream | inspection |
| `stream_filter_builder<Layer>` | stream editing | terminating + clear action right |
| `stream_control_filter_builder<Layer>` | connection-level stream control | unknown |

There is no public engine handle, commit/abort method, action-type field, or
native condition array.

## Lifetime and IRQL

- Policy management and callout registration/unregistration are
  `PASSIVE_LEVEL`.
- Classify, flow association, packet reference/clone, stream copy/clone, and
  injection submission follow their WFP layer contract and may run at
  `DISPATCH_LEVEL`. Keep those paths resident, allocation-aware, nonblocking,
  and exception-free.
- Injector destruction waits for rundown and is `PASSIVE_LEVEL`.
- `callout_driver::reset()` closes flow association, requests removal of every
  tracked context, waits for synchronous or asynchronous flow-delete
  callbacks, and only then unregisters in reverse registration order and
  deletes its unnamed network device. It returns the native failure if WFP
  cannot drain or unregister. Invoke and check it from the driver's unload
  path after user-mode policy has been removed.
- A `flow_target` is a registration-scoped capability. Do not retain or use it
  after its owning `callout_driver` begins reset.

## Verification

[`examples/wfp/ale-connect-block`](../../examples/wfp/ale-connect-block) is
the first runtime sample. Its
[Korean walkthrough](../../examples/wfp/ale-connect-block/README.ko-KR.md)
explains the driver, controller, WFP engine, and nine-step execution sequence.
It registers an `ALE_AUTH_CONNECT_V4` callout, installs all policy objects in
one dynamic transaction, proves a selected loopback TCP connection is denied,
closes the session, and proves connectivity is restored.

[`test/wfp/compile`](../../test/wfp/compile) compiles all typed layer families,
flow-context transfer, stream state machines, ALE pending ownership, and the
three asynchronous injector types with `/W4 /WX`. Its user-mode semantic
contract executes fragmented cursor, edit, subview, copy, early-stop, and
cross-fragment pattern cases on x64 and x86.

The advanced VM gate loads `datagram-proxy`, `async-inspection`,
`flow-monitor`, `stream-edit`, `connect-redirect`,
`tls-inspection-proxy`, `udp-content-filter`, and `tcp-content-filter`
together under Driver Verifier and executes 20 controller iterations per
sample. The two content-filter samples prove independently
that a user-mode coroutine can decide a complete UDP datagram or a complete
message of an explicitly selected TCP application protocol without receiving
native WFP action authority. A TCP block drops the flow.
The connect-redirect sample proves the separate proxy path: the driver
redirects a selected TCP connect, the app captures the original endpoint and
opaque WFP records, and two IOCP coroutines relay the byte stream before
dynamic policy removal restores a direct connection.
The TLS inspection-proxy composes the same safe redirect handoff with two
user-mode Schannel sessions. It observes a fragmented ClientHello, selects
and caches a CA-signed per-SNI leaf, frames a bounded HTTP/1.1 plaintext
request, proves both permit and block outcomes, leaves the trust store
unchanged, and restores a direct TLS connection after policy removal.
The Internet-dependent `browser-https-inspection` project uses independent WFP
keys and service names. Its runtime wrapper owns isolated Edge launch,
temporary test-CA trust, and HTML-log cleanup. The deterministic advanced
Verifier gate exercises the generic TLS proxy, while the HTTPS live runner
exercises the browser workflow.
The gate also runs the
fragmented UDP
NBL/MDL and bounded coroutine-reader contracts at driver load, checks
load/unload accounting plus crash events and dumps, and restores the exact
caller-supplied Verifier targets after its second automatic guest restart.

The mapping to Microsoft's network/trans samples and the exact runtime status
is maintained in
[`test/wfp/WDK-SAMPLE-COVERAGE.md`](../../test/wfp/WDK-SAMPLE-COVERAGE.md).
