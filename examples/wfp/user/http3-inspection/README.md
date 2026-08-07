# User-mode HTTP/3 inspection

[한국어](./README.ko-KR.md) · [WFP samples](../../README.md)

This example separates the product path from its acceptance traffic:

- `crtsys_wfp_http3_inspection_driver.sys` is an app-scoped IPv4/IPv6
  `ALE_AUTH_CONNECT` WFP gate with bounded telemetry.
- `crtsys_wfp_http3_inspection_service.exe` owns the dynamic WFP policy and
  performs real MsQuic TLS 1.3/HTTP/3, QPACK, content decoding, and
  WebTransport processing in user mode.
- The controlled client, traffic generator, result assertions, and `PASS`
  markers live under
  `test/wfp/runtime/fixtures/user/http3-inspection`, not in the product
  executable.

The service handles bounded HTTP/3 framing, static and dynamic QPACK (including
blocked-stream resume and decoder acknowledgement), gzip, deflate, and Brotli
HTML, and an `X-NTL-Block: 1` permit/block policy. Its WebTransport path covers
Extended CONNECT, bidirectional and unidirectional streams, HTTP Datagrams,
Capsule reassembly across fragmented DATA frames, reliable reset mapping, and
rejection before session activation. UDP datagrams are never treated as whole
HTTP messages. Stream, session, and connection ownership provide exact connection shutdown
after all accepted work has drained.

The WFP policy is scoped to the controlled executable, process, address family,
protocol, and selected loopback port. The service publishes numeric evidence;
it does not decide that an acceptance run passed. The external fixture checks
IPv4 and IPv6 classify deltas, original ports, application/process identity,
dynamic policy removal, fail-closed behavior for an unavailable callout, and
zero origin accepts in that negative case.

## Reusable owning server and connection adapter

The product executable uses `ntl::net::http3::msquic_backend::runtime`,
`configuration`, and `server`. The server owns the listener, accepted
connections, sink callbacks, and their native MsQuic state. Closing the runtime
or configuration facade first does not invalidate an accepted connection, and
`server::close()` is idempotent: it rejects new accepts and schedules tracked
cleanup without requiring a detached worker or a native-handle destruction
order in application code. A caller uses `server::drain()` only when later code
must observe completed shutdown, as this service does before publishing its
final evidence.

The sink factory is the only protocol-specific assembly in the listener path:

```cpp
server.open(
    runtime,
    [configuration](const auto &) { return ntl::ok(configuration); },
    [origin, policy](auto transport, const auto &peer)
        -> ntl::result<std::shared_ptr<ntl::net::quic::backend_sink>> {
      ntl::net::http::inspection_session_metadata session;
      session.tls.server_name = std::string(peer.server_name);
      session.tls.alpn = "h3";
      auto proxy = ntl::net::http3::proxy_connection::create(
          std::move(transport), origin, policy, std::move(session));
      if (!proxy)
        return ntl::unexpected(proxy.status());
      return ntl::ok(std::static_pointer_cast<
                     ntl::net::quic::backend_sink>(*proxy));
    });
```

The raw controlled MsQuic peer used to generate and assert acceptance traffic
is test-only under `test/wfp/runtime/fixtures/user/http3-inspection`.

The ordinary service uses `ntl::net::http3::proxy_connection`. Product code
supplies an `ntl::net::http::inspection_policy`, an origin transport, and
optional telemetry;
the adapter owns SETTINGS/control streams, bounded static and dynamic QPACK,
request/trailer assembly, content coding, response framing, GOAWAY, and
WebTransport session routing. Request and response decisions see the semantic
message after transforms in the same `headers`, `body_chunk`,
`message_complete` order used by the HTTP/1.1 and HTTP/2 adapters.

The core origin contract is `async_origin_transport`. Each request stream is
submitted independently, so a delayed origin does not stop another stream and
responses may complete out of order. Backend callbacks and completion callbacks
are serialized inside one connection. Reset, drain, stop, and close cancel
pending exchanges; racing late or duplicate completions are ignored. A
completion may outlive its request. The owning connection retains its backend
and origin state, and `close()` rejects new work while callbacks and children
drain, so callers do not preserve a destruction order.

`immediate_origin_transport_adapter` wraps a synchronous origin for bounded,
deterministic fixtures. It invokes completion immediately and therefore blocks
the submitting callback for the duration of that origin call; a product service
that must preserve HTTP/3 cross-stream concurrency supplies an asynchronous
origin transport instead.

## Build outputs

One configure/build produces the driver, product service, and acceptance tools
in the same configuration directory:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The important targets are:

- `crtsys_wfp_http3_inspection_driver`
- `crtsys_wfp_http3_inspection_service`
- `crtsys_wfp_http3_inspection_acceptance`
- `crtsys_wfp_http3_replay_contracts`

The replay contract is install-free and transport-independent. The service and
acceptance require an architecture-matching official `msquic.dll`; the CMake
header target does not deploy that DLL.

## Product service interface

The service interface is intentionally explicit so a controller can choose one
bounded policy/scenario at a time:

```text
crtsys_wfp_http3_inspection_service.exe
  <controlled-app.exe> <controlled-pid> <ipv4|ipv6>
  <ordinary|webtransport|webtransport-block|handshake>
  <normal|direct|unavailable> <ipc-directory>
```

For the complete disposable-VM acceptance invocation and expected markers, see
the [user HTTP/3 fixture README](../../../../test/wfp/runtime/fixtures/user/http3-inspection/README.md).

This is a controlled endpoint and WFP gate, not transparent interception of an
arbitrary remote UDP/443 flow. ECH, certificate pinning, arbitrary client
identity selection, and bidirectional transparent UDP NAT remain separate
product concerns.
