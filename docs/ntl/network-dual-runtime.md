# NTL network dual-runtime model

[한국어](./network-dual-runtime.ko-KR.md) · [Back to NTL docs](./README.md)

NTL shares an API between user and kernel code when doing so preserves both
semantics and cost. It does not force identical signatures or implementations
when allocator, scheduler, transport, IRQL, or cryptographic-provider
differences would make either side slower or less safe.

```text
bounded protocol core         user direct | kernel direct
policy / transform contract   user direct | kernel direct | explicit offload
OS and codec backend          Winsock/WSK, Schannel, MsQuic, zlib/Brotli, RPC
```

The aggregate kernel header is
[`<ntl/net/kernel/all>`](../../include/ntl/net/kernel/all).
It includes only supported kernel surfaces. Existing user convenience headers
remain available individually.

## Sharing rule

- Protocol values, parsers, message models, policy callbacks, verdicts, and
  transform results use one implementation when the same work is valid in
  both environments.
- `crtsys` supports MSVC STL containers, `std::function`, smart pointers, and
  C++ exceptions in drivers. NTL may therefore use the same owning C++ API on
  a documented `PASSIVE_LEVEL` path instead of maintaining an artificial
  kernel-only container library.
- A WFP callback that arrives above `PASSIVE_LEVEL` uses a resident bounded
  fast path or defers to a passive worker before invoking allocation-owning,
  callback-heavy, or exception-capable code.
- User and kernel transports may expose the same high-level connection and
  stream API while selecting explicit `user_*` and `kernel_*` backends for
  different native handles, callbacks, and shutdown rules.
- An API is split when sharing it would add copying, allocation, locking, or
  capability checks to a hot path that does not otherwise need them.

## Public ownership contract

- Ordinary factories and callbacks retain the provider, runtime, credential,
  policy, sink, workspace, and native state they use. Member declaration and
  facade destruction order are not part of the contract.
- `close()` is idempotent and stops admission. Work accepted before close
  keeps the native state alive until its child objects and callbacks finish.
- Kernel final release through `DISPATCH_LEVEL` queues allocation-free cleanup
  to the driver runtime's joined PASSIVE domain. Applications do not create a
  detached cleanup item or manage rundown for these objects.
- A name containing `borrowed_`, `_view`, or `_ref` is explicitly non-owning.
  Such values are intended for a synchronous callback or low-level adapter;
  copy the data or use an owning factory when it must outlive that scope.
- The normal examples use owning facades. Files that demonstrate a native ABI
  boundary call only APIs whose names explicitly identify borrowed inputs.

## What runs directly in the kernel

| Facility | Kernel contract |
|---|---|
| Fragmented buffers and framing | `scatter_view`, `borrowed_bounded_writer`, caller-owned storage |
| Async receive and coroutines | fixed-capacity `async_byte_stream`, one-reader enforcement, timeout/cancel, PASSIVE_LEVEL resume, owning task state, and optional deterministic drain |
| Full-duplex coroutine lifetime | lazy `bidirectional_status_task` branches joined by `join_bidirectional`; cancel-on-error and join-before-resume are enforced |
| HTTP/1 | bounded request/response framing |
| HTTP/2 | frame parsing and HPACK primitives |
| HTTP/3 | QUIC varints, frame/capsule/datagram parsing, static and Huffman QPACK with caller scratch |
| gRPC | five-byte message framing and allocation-free encoding |
| WebSocket | frame validation and allocation-free unmasking |
| WebTransport | negotiation, stream prefixes, capsules, and quota guard |
| TLS | bounded ClientHello, SNI, ALPN, and ECH-extension observation |
| Policy and rewrite | `borrowed_transform_pipeline` with fixed callbacks, typed verdicts, and caller-owned output |
| TCP/byte transport | `io::transport_backend` and its callback provider seam for Winsock, WSK, or offload |
| QUIC | `ntl::net::quic::transport_backend` semantic contract and callback provider seam |

Direct kernel backends include WSK TCP listeners and clients,
`async_transport_stream`, Schannel TLS client/server termination, CNG/DER
X.509, zlib/gzip/Brotli codecs, and official-NMR MsQuic with HTTP/3/QPACK.
Each backend documents native-provider, `PASSIVE_LEVEL`, bounded-memory,
key-lifetime, cancellation, and shutdown contracts. Browser process launch,
trust-store lifetime, and product policy remain user-mode responsibilities.

### Kernel Schannel credential lifetime

`kernel::schannel` owns every native credential and retires it through the
driver runtime's joined PASSIVE_LEVEL cleanup domain. Credential creation
still occurs at `PASSIVE_LEVEL`, while a
credential handle may be copied and destroyed at any IRQL through
`DISPATCH_LEVEL`. Closing `schannel` invalidates and frees its native handles;
credential handles that are destroyed later remain safe and inert. Member
declaration order is therefore not part of the API contract. Copies share the
same credential: releasing one copy leaves the others valid, and releasing the
last copy schedules native cleanup on that runtime domain.

```cpp
class tls_service {
  std::vector<ntl::net::kernel::schannel_credentials> identities_;
  ntl::net::kernel::schannel schannel_;

public:
  ntl::status add_identity(
      const ntl::net::kernel::schannel_certificate_store_ref &certificate) {
    auto acquired = schannel_.try_server(certificate);
    if (!acquired)
      return acquired.status();
    identities_.push_back(std::move(*acquired));
    return ntl::status::ok();
  }

  void shutdown() noexcept { (void)schannel_.close(); }
};
```

`close()` stops new credentials and waits for in-progress native credential
use and PASSIVE_LEVEL cleanup. The driver runtime joins the cleanup worker
before unload, and the destructor initiates the same credential shutdown.
Transport-first shutdown is useful when a service wants to
observe graceful protocol completion, but it is not a memory-safety or member-
declaration-order requirement. The structured transport helper below provides
that deterministic path.

### Structured async transport lifetime

Request- and session-sized kernel operations should use
`io::with_async_transport`. It owns creation and joins every callback after the
operation returns, fails, or throws, so an early `co_return` cannot skip
cleanup:

```cpp
co_return co_await ntl::net::io::with_async_transport(
    backend, 256 * 1024,
    [&](std::shared_ptr<ntl::net::io::async_transport_stream> stream)
        -> ntl::net::kernel::task<ntl::status> {
      co_return co_await inspect(*stream);
    });
```

For TLS, use the stronger `kernel::with_tls_connection` boundary. It owns the
TLS stream inside the managed transport, attempts close-notify after every
return path, and then joins transport callbacks. The operation receives the
TLS stream and its underlying transport; the latter is only needed by tunnel
protocols that must switch to raw full-duplex forwarding.

```cpp
co_return co_await ntl::net::kernel::with_tls_connection(
    backend, 256 * 1024,
    {.maximum_buffered_ciphertext = 256 * 1024},
    [&](std::shared_ptr<ntl::net::kernel::tls_stream> tls,
        std::shared_ptr<ntl::net::io::async_transport_stream>)
        -> ntl::net::kernel::task<ntl::status> {
      const auto handshaken = co_await tls->handshake_client(
          credentials, server_name, protocols, true);
      if (!handshaken.is_ok())
        co_return handshaken;
      co_return co_await inspect(*tls);
    });
```

Directly constructing `kernel::tls_stream` still retains the transport state.
`with_tls_connection()` is preferred for request-sized work because it also
performs deterministic structured shutdown, while a long-lived service may
keep the owning stream facade directly.

A long-lived service may own an `async_transport_stream` directly. Releasing
the last shared owner closes admission and moves provider cleanup to the
runtime PASSIVE domain. `co_await stream.stop_and_drain()` is the optional
deterministic boundary when later code must observe completed shutdown. It
always crosses a work-queue scheduling boundary before the blocking provider
join, including when the receive side is already idle, so a continuation does
not wait for the callback currently resuming it.

### Full-duplex coroutine joins

A custom kernel tunnel represents each direction as a lazy
`bidirectional_status_task` and consumes both through one structured join:

```cpp
co_return co_await ntl::net::kernel::join_bidirectional(
    relay(client, origin),
    relay(origin, client),
    [&]() noexcept {
      client.stop();
      origin.stop();
    },
    &is_expected_disconnect);
```

The branch type intentionally has no `wait()` and no individual
`operator co_await()`. `join_bidirectional()` starts both directions before
cancellation can run, cancels exactly once after the first non-success result,
and resumes the parent only after both branch frames reach final suspend. A
successful branch lets the opposite direction finish normally. The expected
shutdown predicate decides whether a non-success result is reported as a join
failure; it does not suppress cancellation. Ordinary HTTP/TLS inspection-policy
code does not use this API; it is the lifetime boundary for custom full-duplex
tunnel implementations.

The following user convenience families have explicit kernel routes:

| User convenience family | Kernel route |
|---|---|
| owning HTTP/1, HTTP/2, HTTP/3 transforms | the same owning API at `PASSIVE_LEVEL`; bounded caller-storage overloads for hot paths; optional offload for product policy |
| gRPC and WebSocket transforms | the same transform API at `PASSIVE_LEVEL`; bounded framing/payload overloads for hot paths |
| WebTransport session helpers | direct negotiation, stream prefix, capsule, datagram, and quota core over `quic::transport_backend` |
| zlib/Brotli decoder and encoder registries | direct audited kernel codec backend or explicit `decode_content` / `encode_content` offload |
| Schannel stream, acceptor, certificate cache, TLS frontend | direct kernel provider backend where supported, or explicit `tls_terminate` / `issue_certificate` offload |
| Winsock/WSK and MsQuic objects | one semantic API over explicit user Winsock/MsQuic and kernel WSK/MsQuic backends |

The boundary is deliberate at the header level as well:

| User-only headers | Kernel-facing replacement |
|---|---|
| `io/async_socket`, `io/async_framed_stream` | `io::transport_backend` plus `async_byte_stream` and the common framing probes |
| HTTP/1, HTTP/2, HTTP/3 owning/stream transforms | protocol framing plus `borrowed_transform_pipeline`; an async owner can pend work through `offload::async_backend` |
| `grpc/transform`, `websocket/transform`, `websocket/permessage_deflate` | the same framing/rewrite APIs with kernel codecs; bounded hot-path overloads or explicit offload remain available |
| dynamic `http3/qpack`, inspection proxies, WebTransport session/transform | direct execution over `kernel::msquic_provider` and the common QUIC backend, or product-selected offload |
| content decoder/encoder registries and streams | bounded zlib/gzip/Brotli through `kernel/content_codecs`, or explicit `decode_content`/`encode_content` offload |
| Schannel TLS stream/acceptor/certificate/frontend headers | direct `kernel/schannel`, `kernel/tls_stream`, and `kernel/x509` backends, or explicit offload |

Header guards express an audited execution-domain boundary. Shared semantics
live in the common core, native integration uses explicit backends, and
offload remains a policy choice where it is safer or operationally preferable.

This is one semantic API, not one binary implementation copied into both
address spaces. The direct/offloaded path and its limits remain observable.

The paired WFP examples make that rule concrete. Connect redirect, TCP/UDP
content filtering, TLS inspection, browser HTTP/1.1/2/3 inspection, gRPC, and
WebTransport use one shared semantic policy per pair. User mode and kernel mode
select different native transports and schedulers, but do not redefine the
permit/block/drop or transform rules.

## Build and distribution contracts

Source-based CMake projects pass `KERNEL_CONTENT_CODECS` to
`crtsys_add_driver` only when direct gzip/deflate/Brotli is required. A kernel
driver that uses MsQuic passes `KERNEL_MSQUIC` in the same call. Those options
select the driver-safe codec headers and archives, or the Windows 10
version-2004 target, pinned `crtsys_ntl_msquic_headers` ABI, and `netio.lib`
NMR client import, respectively. User-mode targets can call
`crtsys_add_ntl_msquic_headers()` directly. Neither path installs a DLL or
kernel provider.

Visual Studio/NuGet `NTL WFP` projects automatically receive the kernel codec
headers, `Z_SOLO`, and the two driver-safe archives. User applications receive
the distinct user-mode zlib/Brotli archives. The offline prebuilt bundle ships
both layouts. NuGet and the offline bundle also carry the exact pinned
`msquic.h`; their build integration exposes it to user and kernel consumers
without installing any runtime provider. A driver that actually uses the
kernel provider sets `CrtSysUseNtlKernelMsQuic=true`; that explicit choice
selects the Windows 10 version-2004-or-newer contract and links the NMR client
import surface. Package tests compile the user
HTTP/3 backend and kernel NMR wrapper against that verified ABI and link the
real codec registries instead of checking file presence alone.

## Direct versus offloaded is observable

[`runtime_descriptor`](../../include/ntl/net/runtime) carries the execution
domain, direct/offloaded path, feature bits, and hard limits. A backend must
advertise the requested feature. There is no implicit fail-open or silent
fallback.

```cpp
ntl::net::runtime_descriptor service{
    .domain = ntl::net::execution_domain::user,
    .path = ntl::net::execution_path::offloaded,
    .features = ntl::net::feature_set(
        ntl::net::network_feature::content_transform),
};

auto status = service.require(
    ntl::net::network_feature::content_transform,
    ntl::net::execution_path::offloaded);
```

[`offload::request_header`](../../include/ntl/net/offload/protocol) and its
response are versioned, pointer-free, fixed-width records. They carry request
and flow IDs, protocol, content kind, direction, ports, byte bounds, timeout,
fail-closed intent, and a typed `permit` / `block` / `drop_flow` verdict. NTL RPC reliable
notifications are the control plane; shared memory may be selected as the
bounded data plane for larger bodies. See [IPC](./ipc.md) and the WFP TCP/UDP
content-filter examples.

`offload::backend` is synchronous and is therefore restricted to a waitable
`PASSIVE_LEVEL` path. `offload::async_backend` is the kernel classification and
transport seam: accepted input/output storage remains alive through one
possibly-inline completion, cancellation still completes exactly once, and
`stop()` plus `drain()` closes driver unload. It can be bound to the reliable
NTL RPC flow used by the content-filter examples without putting protocol
parsers, codecs, certificate keys, or a blocking service call in a classify
callback.

## One transform API in both environments

```cpp
struct byte_policy {
  std::string_view blocked_text;
};

ntl::status inspect_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  return input ? ntl::status::ok()
               : ntl::status{STATUS_INVALID_PARAMETER};
}

ntl::result<std::size_t> copy_content(
    void *, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input,
    std::span<std::byte> output) noexcept {
  if (output.size() < input.size())
    return ntl::unexpected(STATUS_BUFFER_TOO_SMALL);
  const ntl::status copied =
      input.bytes().copy_to(output.first(input.size()));
  if (!copied.is_ok())
    return ntl::unexpected(copied);
  return ntl::ok(input.size());
}

ntl::result<ntl::net::inspection::verdict> decide_content(
    void *opaque, const ntl::net::transform_context &,
    ntl::net::inspection::content_view input) noexcept {
  const auto &policy = *static_cast<const byte_policy *>(opaque);
  const auto blocked = input.contains(policy.blocked_text);
  if (!blocked)
    return ntl::unexpected(blocked.status());
  return ntl::ok(*blocked ? ntl::net::inspection::verdict::block
                          : ntl::net::inspection::verdict::permit);
}

ntl::result<ntl::net::transform_result> apply_byte_policy(
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input,
    std::span<std::byte> caller_owned_output,
    byte_policy &policy) noexcept {
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline
      .inspect({&inspect_content, &policy})
      .transform({&copy_content, &policy,
                  ntl::net::execution_path::direct})
      .decide({&decide_content, &policy});
  return pipeline.run(context, input, caller_owned_output);
}
```

The pipeline stores no `std::function`, allocates no memory, and does not own
callback state. `byte_policy` must therefore outlive `apply_byte_policy()`.
The caller also supplies bounded output storage; insufficient storage is an
explicit error. `content_view` may refer to fragmented storage, but it must
already represent a complete unit selected by the caller, such as one UDP
datagram or one message produced by a TCP framer. This byte pipeline does not
implicitly reassemble TCP or interpret HTTP.

For HTTP method, path, header, body, and connection metadata rules, use the
semantic [`http::inspection_policy`](../../include/ntl/net/http/inspection_policy)
above the HTTP/1.1, HTTP/2, or HTTP/3 adapter:

```cpp
void configure_http_policy(ntl::net::http::inspection_policy &policy) {
  namespace condition = ntl::net::http::condition;
  policy.requests()
      .at_headers()
      .when(condition::method_is("POST"))
      .when(condition::path_is("/inspect"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
}
```

These are two intentionally different abstraction levels:
`net::borrowed_transform_pipeline` is the allocation-free byte/content primitive,
while `net::http::inspection_policy` is the semantic HTTP rule and rewrite
API.

User-service policy is a normal typed stage rather than a special WFP action:

```cpp
ntl::net::inspection::verdict decide_with_service(
    std::shared_ptr<ntl::net::offload::backend> service,
    const ntl::net::transform_context &context,
    ntl::net::inspection::content_view input) noexcept {
  ntl::net::offload::inspect_adapter service_policy(
      std::move(service), 2'000);
  ntl::net::borrowed_transform_pipeline pipeline;
  pipeline.decide(service_policy.stage());

  const auto decision = pipeline.run(context, input);
  if (!decision)
    return service_policy.failure_verdict();
  return decision->verdict;
}
```

## QUIC backends

[`ntl::net::quic`](../../include/ntl/net/quic/transport) defines the common
transport contract consumed by HTTP/3.
[`borrowed_callback_transport`](../../include/ntl/net/quic/borrowed_callback_transport) is the
allocation-free provider seam for kernel or user stacks. The concrete MsQuic
connection types are in `ntl::net::http3::msquic_backend`; kernel NMR provider
acquisition and lifetime are managed by
[`ntl::net::kernel::msquic_provider`](../../include/ntl/net/kernel/msquic).

[MsQuic officially supports Windows kernel mode](https://microsoft.github.io/msquic/msquicdocs/docs/Platforms.html),
and its public API contains kernel-specific status definitions, IRQL
annotations, and a common function-table object model. NTL uses the same
connection/stream semantics in both execution domains. NTL does
not assume that an undocumented import contract to the inbox `msquic.sys` is
stable: the kernel backend must use a
documented provider/import contract or a pinned, reproducible official MsQuic
kernel build. Explicit user-service offload remains an option, not the only
kernel QUIC route.

## Kernel lifetime rules

- Network callbacks expose views only for the callback lifetime.
- Retained bytes require bounded nonpaged ownership or caller storage.
- Large bounded scratch belongs in
  [`kernel::workspace_pool`](../../include/ntl/net/kernel/workspace_pool), not
  in a kernel stack frame. `try_acquire()` returns a move-only RAII lease backed
  by a nonpaged lookaside list; callers do not manage native pool memory.
- `http::inspection_resource_profile` is the single source of parser and
  workspace limits. Leaving a workspace budget at zero derives the required
  size; an explicit undersized budget is rejected with a protocol-specific
  diagnostic before traffic is accepted.
- The kernel HTTP dispatcher performs the PASSIVE_LEVEL transition and stack
  isolation internally. Applications call the inspection API directly and do
  not wrap HTTP/1, HTTP/2, HTTP/3, gRPC, or codec work in `expand_stack()`.
- Kernel HTTP/1 and HTTP/2 sessions deliberately split memory ownership. Large
  wire/frame buffers come from a caller-owned fixed nonpaged workspace lease;
  an exhausted active-lease quota fails the flow closed. Variable semantic
  messages, headers, bodies, trailers, and transformed wire output receive a
  `http::message_memory_ref` resource. Kernel sessions bind that resource to a
  `borrowed_bounded_memory_resource` backed by the crtsys nonpaged allocator, with both
  aggregate and single-allocation limits. HTTP/3 applies the same contract per
  connection. A limit breach maps `std::bad_alloc` to
  `STATUS_INSUFFICIENT_RESOURCES` and closes the affected stream or flow rather
  than silently forwarding it. Codec work has an additional independent byte,
  allocation, output, and expansion-ratio budget. Peak semantic allocation is
  observable for runtime diagnostics. User mode keeps the same message API and
  uses the default PMR resource unless it opts into an explicit bounded resource.
  Each exchange releases its semantic state, and shutdown tests require a
  completed structured transport drain followed by zero active workspace
  leases.
- `http2::prepare_kernel_proxy_session` evaluates the first downstream request
  before an origin connection is opened. Its move-only preflight result owns
  the workspace lease and any bounded origin-facing frames. A local block is
  flushed without contacting the origin; a permitted result can be consumed
  exactly once by `run_kernel_proxy_session`. Browser-facing sessions may
  advertise Extended CONNECT in their local SETTINGS; the adapter consumes
  only the matching local SETTINGS acknowledgement and leaves the later
  origin SETTINGS exchange intact.
- Allocation-free parsers may run only where all input and callback state are
  resident and the surrounding native API permits that IRQL.
- Allocation-owning/stateful work runs at `PASSIVE_LEVEL`.
- [`kernel::executor`](../../include/ntl/net/kernel/executor) closes posting
  against shutdown and drains all accepted callbacks before driver unload.
- `async_transport_stream` closes and drains through the runtime PASSIVE
  cleanup domain when its last owner releases it; direct request/session
  operations use `with_async_transport` for deterministic completion. Explicit
  `stop_and_drain()` remains available when later code must observe that all
  provider callbacks have left the operation.
- Quotas, timeout, cancellation, and fail-open/fail-closed policy are explicit.

## Examples and tests

- [`test/net/kernel-contracts`](../../test/net/kernel-contracts/) executes the
  same gRPC and transform contracts in an app and a driver over a synthetic
  IOCTL test transport. It also owns the kernel coroutine byte-stream tests
  for fragmented delivery, timeout, cancellation, EOF, competing readers,
  capacity limits, and unload drain, keeping synthetic traffic out of the
  examples.
- [`examples/wfp/kernel/flow-monitor`](../../examples/wfp/kernel/flow-monitor/)
  contains only the WFP flow/stream observation policy and telemetry path;
  the runtime fixture creates the IPv4/IPv6 traffic used to verify it.
- [`examples/wfp/user/tcp-content-filter`](../../examples/wfp/user/tcp-content-filter/)
  and [`user/udp-content-filter`](../../examples/wfp/user/udp-content-filter/)
  demonstrate user-policy offload. Their [`kernel/tcp-content-filter`](../../examples/wfp/kernel/tcp-content-filter/)
  and [`kernel/udp-content-filter`](../../examples/wfp/kernel/udp-content-filter/)
  counterparts run bounded framing and content verdicts directly in the driver.
- The paired [`user/browser-https-inspection`](../../examples/wfp/user/browser-https-inspection/)
  and [`kernel/browser-https-inspection`](../../examples/wfp/kernel/browser-https-inspection/)
  examples apply the same `http::inspection_policy` to HTTP/1.1, HTTP/2, and
  HTTP/3. The user service supplies Winsock, Schannel, and MsQuic; the kernel
  driver supplies WSK, kernel Schannel, and the MsQuic NMR backend. Both cover
  permit/block/drop, header/body rewrite, content decoding, and bounded HTML
  capture without requiring browser launch or setting changes.
- [`kernel/tls-inspection-proxy`](../../examples/wfp/kernel/tls-inspection-proxy/)
  is the smaller WSK/Schannel counterpart for studying the redirected TLS
  boundary without the browser aggregate.
- [`kernel/http3-inspection`](../../examples/wfp/kernel/http3-inspection/)
  exercises real kernel QUIC/TLS 1.3, SETTINGS, QPACK, and dual-stack WFP policy
  over the official MsQuic NMR provider.
- `test/wfp/compile/kernel.cpp` compiles the aggregate kernel header under
  `/W4 /WX`.
- `test/wfp/compile/protocols.cpp` runs the same corpus in user mode.
- `test/wfp/compile/fuzz.cpp` exercises fragmented HTTP, HPACK/QPACK,
  WebSocket, gRPC, capsules, codecs, and both ClientHello parsers.
