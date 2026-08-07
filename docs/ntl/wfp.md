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

The examples are split by execution boundary instead of mixing both designs
inside one directory:

- [`examples/wfp/user`](../../examples/wfp/user/) keeps protocol parsing,
  certificate policy, and content decisions in a controller or proxy;
- [`examples/wfp/kernel`](../../examples/wfp/kernel/) contains direct kernel
  implementations and the native WFP primitive samples.

TCP/UDP content filtering, connect redirect, TLS inspection, controlled
browser HTTPS capture, and HTTP/3 have paired user/kernel examples. The
kernel versions are separate projects; they do not silently turn a user-mode
example into a different design. Primitive callout examples such as flow
monitoring and stream editing remain kernel-only because an empty user copy
would teach no additional contract.

NuGet `NTL WFP` projects automatically link the package's audited kernel
zlib/Brotli archives. Source-based CMake projects opt in only when needed:

```cmake
crtsys_add_driver(
  my_callout WFP NTL KERNEL_CONTENT_CODECS src/main.cpp)
```

Kernel drivers opt in to the pinned MsQuic ABI, Windows 10 version-2004 target,
and NMR client import as one driver option:

```cmake
crtsys_add_driver(
  my_h3_callout
  WFP NTL KERNEL_MSQUIC KERNEL_CONTENT_CODECS
  src/main.cpp)
```

`KERNEL_MSQUIC` supplies headers and `netio.lib`, not a provider. A user-mode
CMake target that only needs the pinned ABI can still call
`crtsys_add_ntl_msquic_headers()` and link `crtsys_ntl_msquic_headers`.
NuGet and the offline prebuilt bundle carry the same SHA-256-verified header
and add its include directory automatically. User applications deploy a
compatible `msquic.dll`; kernel NuGet consumers opt in with
`<CrtSysUseNtlKernelMsQuic>true</CrtSysUseNtlKernelMsQuic>`, which selects the
Windows 10 version-2004-or-newer contract and `netio.lib`, and bind to a
compatible, explicitly installed MsQuic NMR provider.
Building never installs or starts either one.

## Rules enforced by the API

### Layers and keys never become raw GUIDs

Provider, sublayer, callout, and filter keys are distinct types. Callout and
filter keys also contain their layer type:

```cpp
using layer = ntl::wfp::layers::ale_auth_connect_v4;

constexpr GUID native_callout = /* project-owned stable GUID */;
constexpr ntl::wfp::arbitrating_callout_key<layer> callout(native_callout);
```

A key for `stream_v4` cannot be passed to an `ale_auth_connect_v4`
registration or filter. Native GUID access is private and is available only to
the registration and policy writers.

### A classify callback returns the decision allowed by its filter

Ordinary classify callbacks receive a non-copyable, callback-scoped,
read-only `classify_event<Layer>`. The callout key and callback result encode
the same native WFP action contract:

```cpp
constexpr auto authorize =
    +[](const ntl::wfp::classify_event<layer>& event) noexcept {
      const auto port =
          event.value(layer::field::remote_port).uint16();
      return port && *port == 443
                 ? ntl::wfp::arbitration_decision::block
                 : ntl::wfp::arbitration_decision::continue_classification;
    };
```

The callback cannot access `FWPS_CLASSIFY_OUT0`. The trampoline applies
`FWPS_RIGHT_ACTION_WRITE`, clear-action-right, veto, and absorb semantics.
The controller and driver must use the same typed key:

- `inspection_callout_key` / `add_inspection()`: a `void` observer callback;
  WFP classification continues automatically;
- `terminating_callout_key` / `terminating_decision`: permit or block; and
- `arbitrating_callout_key` / `arbitration_decision`: continue, permit, or
  block (`FWP_ACTION_CALLOUT_UNKNOWN`).

`stream_callout_key` is paired with `stream_result` and the fixed stream
UNKNOWN action. An observation-only stream callback instead uses
`inspection_callout_key` and `add_stream_inspection()`; it returns `void`.
Callbacks are `noexcept`. Enforcement callbacks must return exactly the
decision type selected by the key, while inspection callbacks return exactly
`void`. A mismatched controller filter, driver registration, or callback
return type does not compile.

The supported decision layers are:

- `ale_connect_redirect_v4` and `ale_connect_redirect_v6`;
- `ale_auth_connect_v4` and `ale_auth_connect_v6`;
- `ale_auth_recv_accept_v4` and `ale_auth_recv_accept_v6`;
- `ale_flow_established_v4` and `ale_flow_established_v6`;
- `datagram_data_v4` and `datagram_data_v6`; and
- inbound/outbound transport v4 and v6; and
- `outbound_ip_packet_v4` and `outbound_ip_packet_v6`.

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

`callout_driver::add_stream<Context>()` and
`add_flow_context<Context>()` return a
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

`network_injector`, `transport_injector`, and `stream_injector` own shared
injection state. Successful asynchronous injection transfers the packet owner
to a completion context. `injection_limits::maximum_in_flight` is enforced
before submission; exhaustion returns `STATUS_QUOTA_EXCEEDED` without
consuming the caller's packet. `close()`/destruction rejects new work, while
the native handle remains alive until every accepted completion drains. A last
release at APC or DISPATCH is retired to the runtime PASSIVE cleanup domain,
so callers do not schedule cleanup work or remember a destruction IRQL.

Outbound transport injection also uses a move-only
`transport_send_request`. It deep-copies or adopts the remote address and
ancillary control data together with the endpoint, address family, scope, and
compartment. `transport_injector::inject_send()` moves both the clone and that
request into the same completion context. This is required because WFP keeps
the buffers referenced by `FWPS_TRANSPORT_SEND_PARAMS0` until asynchronous
completion; stack-backed native parameter blocks are therefore not exposed by
the NTL API.

```cpp
auto request = ntl::wfp::transport_send_request::try_copy(
    endpoint, AF_INET6, compartment, remote_address, remote_scope,
    control_data, ntl::net::buffer_limits{4096});
if (!request)
  return ntl::wfp::terminating_decision::block_and_absorb;

const auto injected = injector.inject_send(
    std::move(clone), std::move(*request));
```

Synchronous submission failure destroys the completion context immediately.
Successful submission destroys it from the WFP completion callback. Injector
reset stops new submissions; rundown and PASSIVE cleanup keep accepted packet
owners and native state alive through their callbacks. None of those paths
needs caller-managed buffer lifetime or a second cleanup branch.

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
- `borrowed_byte_cursor` performs bounded sequential and big-endian reads;
- `scan_bytes()` finds a fixed token across fragment boundaries without
  flattening; and
- `owned_bytes` is a move-only nonpaged deep copy with a mandatory
  `buffer_limits` check.

Packet wrappers expose the distinction directly:

```cpp
const auto borrowed = event.packet();
ntl::net::borrowed_byte_cursor header(borrowed.bytes()); // callback lifetime only

auto saved = borrowed.try_copy(ntl::net::buffer_limits{64 * 1024});
if (!saved)
  return ntl::wfp::terminating_decision::block_and_absorb;
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
    co_await stream.read_exactly_borrowed(body->span());
```

Producers call `append_received_data(scatter_view)`. The call copies fragments
into nonpaged storage before returning, rejects capacity overflow, and never
retains a borrowed native pointer. Awaited continuations resume through a
system work queue at `PASSIVE_LEVEL`. EOF, cancellation, timeout, capacity
overflow, and a competing reader are reported as `NTSTATUS`; two work slots
prevent a second completed read from reusing a work item that is still
resuming the first.

Coroutine-frame ownership belongs to the driver's task or flow lifetime. The
destination span and frame must survive suspension. At teardown, the owner
`co_await`s `cancel_and_drain()` and only then releases the stream and task.
The drain continuation always resumes through a separate `PASSIVE_LEVEL` work
item, so it is safe when requested by the read continuation currently being
resumed. Destroying a task while its continuation is queued is invalid.

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
`co_await read_some_borrowed()`, `read_exactly_borrowed()`, and `write_all()`.
The borrowed names make the destination-span lifetime explicit. It does not share
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

`policy_session` makes lifetime an explicit construction choice. An
`ephemeral()` session installs process-scoped objects that BFE removes if the
controller exits or loses its engine connection:

```cpp
auto session = ntl::wfp::policy_session::ephemeral(
    L"my product policy");
session.install([](ntl::wfp::policy_transaction& tx) {
  const auto provider = tx.add_provider(/* provider_spec */);
  const auto sublayer = tx.add_sublayer(provider, /* sublayer_spec */);
  const auto callout =
      tx.add_callout<layer>(provider, /* callout_spec<layer> */);

  ntl::wfp::filter_builder<layer> filter(
      /* filter_key<layer> */, L"Block selected TCP port");
  filter.protocol_equal(IPPROTO_TCP)
      .remote_address_equal(
          ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
      .remote_port_equal(443);
  tx.add_filter(sublayer, callout, filter);
});
```

Normal return commits. Any exception aborts. Provider, sublayer, and callout
references are transaction-scoped capabilities with a generation identity;
mixing references from two transactions or providers throws before a native
filter call.

For product policy that must survive controller or BFE restarts, construct a
`persistent()` session and declare the graph keys once:

```cpp
ntl::wfp::policy_manifest manifest;
manifest.include(provider_key)
    .include(sublayer_key)
    .include(callout_key)
    .include(filter_key);

auto policy = ntl::wfp::policy_session::persistent(
    L"my persistent product policy");
policy.reconcile(manifest, [](ntl::wfp::policy_transaction& tx) {
  // Add the complete replacement graph.
});

const auto health = policy.health(manifest);
if (!health.healthy())
  policy.reconcile(manifest, write_complete_policy);

policy.uninstall(manifest); // explicit product uninstall
```

`reconcile()` removes the manifest's previous filters, callouts, sublayers,
and providers in dependency order and installs the complete replacement in
the same native transaction. Before commit, NTL verifies that the writer
created exactly the keys declared by the manifest; an omitted or undeclared
object aborts the transaction. `install()` is ephemeral-only, while
`reconcile()` and `uninstall()` are persistent-only, so lifetime cannot be
selected accidentally after construction. `health()` reports missing object
keys without changing policy. `reconnect()` reopens the BFE engine;
persistent callers then run `health()` or `reconcile()`. Persistent providers
may specify the owning service name in `provider_spec::service_name`.

The policy builders fix their native action:

| Builder | Layer/use | Native action |
| --- | --- | --- |
| `filter_builder<Layer>` | ALE authorization | terminating + clear action right |
| `inspection_filter_builder<Layer>` | observation only | inspection; callback can only continue |
| `arbitration_filter_builder<Layer>` | flow/packet fail-close or conditional decisions | unknown; callback may continue/permit/block |
| `packet_filter_builder<Layer>` | datagram/transport packet decision | terminating + clear action right |
| `local_udp_proxy_reply_filter_builder<Layer>` | loopback `OUTBOUND_IPPACKET` proxy reply restoration | terminating; unrelated traffic remains available while the callout is absent |
| `stream_filter_builder<Layer>` | stream inspection, editing, and connection control | unknown |

There is no public engine handle, commit/abort method, action-type field, or
native condition array.

`local_udp_proxy_reply_filter_builder` is deliberately narrower than the
generic packet builder. `OUTBOUND_IPPACKET` has no protocol or port policy
condition, so this builder fixes the layer to IPv4/IPv6 outbound IP, installs
the family loopback address, requires a nonzero proxy port as callout context,
and hides the unavailable-callout choice. The matching callout parses the UDP
header and port before it absorbs anything; unrelated loopback traffic is not
blocked when the callout is absent.

Ordinary applications do not assemble the six flow/datagram/reverse objects
themselves. `transparent_udp_proxy_policy::install()` owns a complete policy,
while `add_to()` adds the same indivisible graph to an existing provider and
sublayer. `transparent_udp_proxy_service` owns the matching dual-stack
callouts, tuple table, injection, callback rundown, and idempotent close.
The unrestricted outbound-IP packet builder is available only under
`ntl::wfp::advanced`; the default API cannot create a broad reverse hook.

## Typed conditions

Condition methods exist only when the selected WFP layer supports the native
field. For example, ALE layers expose application, user, package, protocol,
address, and port conditions; stream layers do not expose protocol; MAC
layers expose MAC, EtherType, VLAN, and interface fields. An invalid
layer/condition pair therefore fails during compilation.

Addresses and identities have explicit owning types:

- `ipv4_address::from_octets()` and `ipv4_network`;
- `ipv6_address` and `ipv6_network`;
- `mac_address`;
- `user_identity` and `package_identity`.

Prefix lengths, direction values, empty flag masks, VLAN identifiers, SID
validity, and duplicate native fields are validated before BFE is called.
`icmp_equal(type, code)` adds protocol, ICMP type, and ICMP code atomically so
the caller cannot accidentally create a cross-protocol rule.

## Diagnostics and event telemetry

`policy_session::inspect_filter()` and `enumerate_filters<Layer>()` return
bounded, pointer-free policy snapshots. Application blobs are represented by
size and hash rather than retained executable paths.

`network_event_monitor` subscribes to WFP classify-drop and IPsec-drop events
and copies them into a preallocated bounded ring:

```cpp
ntl::wfp::network_event_monitor events({
    .maximum_queued_events = 2048,
    .manage_collection_state = false,
});

ntl::wfp::network_event_snapshot event;
if (events.wait_pop(event, std::chrono::seconds(1))) {
  // Correlate event.filter_id and event.layer_id with policy diagnostics.
}
```

The callback path performs no queue allocation. Ring overflow drops new
records and increments `dropped_by_limit`. Collection enablement is a
machine-wide BFE option, so the monitor leaves it unchanged by default. Set
`manage_collection_state` only when this component owns that global setting;
the previous value is restored by `stop()`.

## Lifetime and IRQL

- Policy management and callout registration/unregistration are
  `PASSIVE_LEVEL`.
- Classify, flow association, packet reference/clone, stream copy/clone, and
  injection submission follow their WFP layer contract and may run at
  `DISPATCH_LEVEL`. Keep those paths resident, allocation-aware, nonblocking,
  and exception-free.
- Injector facade destruction is IRQL-independent. Accepted completions own
  shared rundown state, and native-handle destruction is transferred to the
  joined runtime PASSIVE cleanup domain.
- `callout_driver::add()` owns the callback object and every explicitly bound
  `shared_ptr` state. `close()` rejects new callback entries, drains callbacks
  and flow contexts, unregisters callouts in reverse order, and then deletes
  its unnamed network device. The operation is idempotent across copied
  facades and reports a native drain or unregister failure. An ordinary
  `PASSIVE_LEVEL` caller receives the completed result. A call from one of the
  driver's own callbacks, or from `APC_LEVEL`/`DISPATCH_LEVEL`, returns the
  successful `STATUS_PENDING` close request; the runtime retains the owner and
  completes the same drain on its joined `PASSIVE_LEVEL` worker.
- A `flow_target` is a registration-scoped capability. Association returns
  `STATUS_DELETE_PENDING` after its owning `callout_driver` begins closing.
- Releasing the last facade above `PASSIVE_LEVEL` transfers native cleanup to
  the runtime PASSIVE cleanup domain. Drivers do not queue detached cleanup
  work items or manage callback rundown themselves.

## Verification

[`examples/wfp/kernel/ale-connect-block`](../../examples/wfp/kernel/ale-connect-block) is
the first runtime sample. Its
[Korean walkthrough](../../examples/wfp/kernel/ale-connect-block/README.ko-KR.md)
explains the driver, controller, WFP engine, and nine-step execution sequence.
It registers an `ALE_AUTH_CONNECT_V4` callout, installs all policy objects in
one ephemeral transaction, proves a selected loopback TCP connection is
denied, closes the session, and proves connectivity is restored. Its runtime
suite also reconciles a persistent manifest, closes the installing controller,
checks health and continued enforcement from a new connection, and explicitly
uninstalls the graph.

[`test/wfp/compile`](../../test/wfp/compile) compiles all typed layer families,
flow-context transfer, stream state machines, ALE pending ownership, and the
three asynchronous injector types with `/W4 /WX`. Sixteen CTest semantic
contracts execute in Debug/Release, including every IPv4/IPv6 prefix length,
deterministic fragmented framing/search inputs, parser fuzz contracts, and
the browser HTTP/3 contract.

The advanced VM gate builds and packages only its selected samples. The
dual-stack policy gate loads `datagram-proxy`, `async-inspection`,
`flow-monitor`, `udp-content-filter`, and `tcp-content-filter` together under
Driver Verifier and executes 20 controller iterations per sample. A full
regression can run every selected advanced sample together. IPv4 and IPv6
redirect, delayed decisions, observation, content verdicts, endpoint closure,
and post-policy restoration run in the gate. The two content-filter samples
prove independently
that a user-mode coroutine can decide a complete UDP datagram or a complete
message of an explicitly selected TCP application protocol without receiving
native WFP action authority. A TCP block drops the flow.
The connect-redirect sample proves the separate proxy path: the driver
redirects a selected TCP connect, the app captures the original endpoint and
opaque WFP records, and two IOCP coroutines relay the byte stream before
ephemeral policy removal restores a direct connection.
The TLS inspection-proxy composes the same safe redirect handoff with two
user-mode Schannel sessions. It observes a fragmented ClientHello, selects
and caches a CA-signed per-SNI leaf, frames a bounded HTTP/1.1 plaintext
request, proves both permit and block outcomes, leaves the trust store
unchanged, and restores a direct TLS connection after policy removal.
The `browser-https-inspection` project uses independent WFP keys and service
names. Its user service terminates Schannel HTTP/1.1 and HTTP/2 plus MsQuic
HTTP/3, then applies one owning inspection/rewrite policy to the decoded
messages. The kernel counterpart runs the same semantic policy with WSK,
kernel Schannel, and the MsQuic NMR backend. Their runtime wrappers observe an
already-running exact browser executable without creating a profile, launching
or terminating the browser, or adding flags, and own HTML-log cleanup. Local
controlled-origin tests are deterministic; the external-origin HTTP/3 probe is
separate because the surrounding network can block QUIC.
The gate also runs the fragmented UDP NBL/MDL contract, checks load/unload
accounting plus crash events and dumps, and requires the exact caller-supplied
Verifier settings to remain byte-for-byte unchanged. Bounded coroutine-reader
contracts live in the dedicated kernel-contract driver instead of a sample.
The runner never starts, resets, reverts, or reboots the VM and never changes
Driver Verifier; the operator selects the required driver-signing startup
option before the run. Low Resources Simulation requires a separately
prepared, operator-controlled boot and separate evidence. It is not implied
by the normal Verifier gate. That separate gate has passed for the kernel
browser inspection path with an observed intentional allocation failure,
fail-closed cleanup, no new crash or dump, and unchanged Verifier settings.

The mapping to Microsoft's network/trans samples and the exact runtime status
is maintained in
[`test/wfp/WDK-SAMPLE-COVERAGE.md`](../../test/wfp/WDK-SAMPLE-COVERAGE.md).
