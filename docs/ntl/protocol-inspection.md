# HTTP and WebSocket protocol inspection

[Back to NTL documentation](./README.md)

NTL separates complete reusable protocol contracts from product-selected
backends. This prevents an application from treating arbitrary plaintext
bytes as a complete HTTP message or silently permitting traffic merely because
a decoder is unavailable.

## Support layers

| Layer | NTL supplies | Application or product supplies |
| --- | --- | --- |
| HTTP/1.1 | Bounded request/response framing, validated length and chunked boundaries | Semantic policy and decoded-body handling |
| WebSocket | Bounded RFC 6455 framing, fragmented-message assembly, semantic rewrite, negotiated `permessage-deflate` decode/re-encode, masking, and output fragmentation | Subprotocol schema and nonstandard negotiated extensions |
| HTTP/2 | Bounded frame validation, HEADERS/CONTINUATION assembly, HPACK, request/response association, flow control, GOAWAY/RST handling, staged policy, and a structured bidirectional proxy session | Semantic policy and optional Extended CONNECT subprotocol handling |
| HTTP/3 | QUIC-varint HTTP/3 frame validation, split-stream assembly, decoded-header/DATA routing, and a bounded zero-dynamic-table QPACK decoder | QUIC transport, stream lifecycle, and a stateful QPACK provider when a dynamic table is negotiated |
| gRPC | Incremental five-byte message framing, negotiated message compression, semantic rewrite, and re-encoding over HTTP/2 or HTTP/3 DATA | Protobuf or other application schema |
| WebTransport | Draft-16 SETTINGS, QPACK Extended CONNECT, real bidirectional/unidirectional streams and HTTP Datagrams over the raw MsQuic backend, plus bounded semantic policy | Application payload schema and deployment trust model |
| Content coding | Bounded decoder/encoder registries, reverse decoding and sender-order encoding of chains, and gzip/RFC 1950 deflate/Brotli adapters | Zstandard, dictionary, or proprietary adapters |
| TLS | Schannel coroutine stream, ClientHello observation, ALPN, managed downstream identity selection, frontend-owned ECH plaintext handoff, explicit mTLS authorization, and bounded audit | Authorized issuer/trust deployment, actual ECH key/configuration provider, and product identity inventory |

`<ntl/net/http2/hpack>` supplies the complete RFC 7541 static table, dynamic table,
integer representation, literal forms, and Huffman decoder. Dynamic-table and
decoded-header sizes are explicit bounds. `<ntl/net/http3/qpack>` supplies the
interoperable profile used when both QPACK dynamic-table settings are zero.
Dynamic QPACK remains a provider contract because blocked header streams,
encoder instructions, and decoder acknowledgements depend on QUIC connection
state.

Keep one `bounded_hpack_decoder` per HTTP/2 connection direction:

```cpp
ntl::net::http2::bounded_hpack_decoder decoder({
    .maximum_dynamic_table_size = 4096
});
ntl::net::http2::borrowed_connection_inspector inspector(decoder);
```

Do not share that decoder with another connection or the opposite direction;
HPACK dynamic tables are directional connection state.

## Parser selection is negotiated

`<ntl/net/tls/inspection_policy>` maps a negotiated ALPN value to exactly one
parser family:

```cpp
auto selected = ntl::net::inspection::select_tls_application_protocol(
    ntl::net::inspection::encrypted_transport::tcp_tls,
    tls.negotiated_application_protocol());
if (!selected.valid_for_transport)
  block_connection();
```

`h2` selects HTTP/2 on TCP, `http/1.1` selects HTTP/1.1, and `h3` selects
HTTP/3 only on QUIC. An empty ALPN selects HTTP/1.1 only when the caller
explicitly enables that legacy fallback. WebSocket is selected later by a
validated HTTP/1.1 Upgrade exchange, not by looking at arbitrary body bytes.

## Fail-closed capability policy

`explicit_tls_inspection_policy` distinguishes these outcomes:

- no usable server name;
- ECH confirmed by an ECH-aware frontend but inner ClientHello not recovered;
- downstream certificate rejection, including pinning;
- origin requests mutual TLS but no authorized client identity exists;
- negotiated protocol has no active adapter;
- content coding has no registered decoder; and
- QUIC selected while no inspection backend is active.

All defaults are `block`. A product may deliberately configure a different
action, but `bypass` means implementing an unchanged ciphertext tunnel; it
does not mean feeding opaque bytes to a plaintext parser. `metadata_only`
likewise requires a product path that records only information genuinely
available at that layer.

The raw presence of extension type `0xfe0d` is not confirmation because GREASE
ECH intentionally uses the same wire shape. Possessing a candidate ECH key is
also not success. A confirmed ECH connection becomes inspectable only after a
frontend has authenticated and decrypted the inner ClientHello and supplied
the resulting server name. Certificate pinning cannot be transparently
defeated by this API. Mutual TLS cannot guess which client identity represents
the user.

`<ntl/net/tls/product_policy>` combines these observations with actual
deployed capabilities. It returns only actions a product can execute:
intercept, tunnel unchanged ciphertext, record metadata, terminate, or block
QUIC for a normal TCP retry. `when_possible` may tunnel a pinned or ECH
connection unchanged; `required` terminates it. Neither mode disables browser
certificate validation or pretends to decrypt opaque traffic.

`<ntl/net/tls/product_backend>` is the corresponding execution boundary. It
returns either a cached Schannel server identity or the owned plaintext channel
created by a successful ECH frontend, after application/host trust
classification. It also wraps origin mTLS selection with bounded audit events.
The API never treats a bare ECH extension, a candidate key, or a pinned client
as successful interception.

## Semantic transforms

`<ntl/net/http/transform>` is shared by HTTP/1.1, HTTP/2, and HTTP/3 complete
messages. `<ntl/net/http/async_transform>` adds a fixed worker pool, bounded
queue, cooperative cancellation, deadlines, overload policy, and statistics.
`<ntl/net/http/stream_transform>` handles bounded plaintext body chunks when a
complete-body allocation is undesirable. Its per-message
`content_encoding_stream` incrementally decodes and re-encodes registered
`Content-Encoding` chains across arbitrary H1 chunk, H2 DATA, or H3 DATA
splits. The corresponding live wire/event adapters are
`<ntl/net/http/http1_stream_transform>`,
`<ntl/net/http2/stream_transform>`, and
`<ntl/net/http3/stream_transform>`. Stateful callbacks are constructed once per
message, so multiplexed streams never share partial application records.
Separate semantic adapters cover
WebSocket (`<ntl/net/websocket/transform>`), gRPC
(`<ntl/net/grpc/transform>`), and WebTransport
(`<ntl/net/http3/webtransport_transform>`).

Owning HTTP messages are allocator-aware without changing the policy callback
shape. User code normally uses the default resource. A bounded or kernel
adapter passes `http::message_memory_ref` to the H1/H2/H3 parser or connection; the
method, target, headers, body, trailers, and rewritten semantic response then
use that resource. `borrowed_bounded_memory_resource` places aggregate and
single-allocation limits over a caller-selected PMR backend, while
`borrowed_fixed_workspace_resource` has no fallback beyond its caller-owned span:

```cpp
std::array<std::byte, 64 * 1024> storage{};
ntl::net::borrowed_fixed_workspace_resource workspace(storage);

auto request = ntl::net::http::parse_http1_request(
    wire, decoders, {.origin_scheme = "https"}, limits,
    ntl::net::http::message_memory_ref{workspace.resource()});
if (!request && request.status() == STATUS_INSUFFICIENT_RESOURCES)
  drop_flow(); // never forward a partially parsed or rewritten message
```

The explicitly borrowed PMR form above requires its workspace to outlive every
derived message and result; its `_ref` and `borrowed_` names mark that boundary.
Ordinary kernel proxy sessions instead take an owning workspace lease and keep
it through coroutine completion. They expose peak allocation counters without
making policy code manage an allocator or release order.

## Context-aware decisions

`<ntl/net/http/inspection_context_view>` avoids making a policy infer meaning from
an untyped body buffer. One `inspection_policy` combines semantic transforms
with ordered decisions at headers, body, and message-complete stages. A rule
can use the request target and headers together with response, connection,
application, WFP flow, and TLS metadata:

```cpp
ntl::net::http::inspection_policy policy;

policy.requests()
    .at_headers()
    .when([](const ntl::net::http::inspection_context_view &context) {
      return context.method() == "POST" &&
             context.path() == "/admin/import" &&
             context.query() == "mode=replace" &&
             context.headers().first("content-type") ==
                 "application/json" &&
             context.connection().application_id &&
             *context.connection().application_id == trusted_browser_app_id &&
             context.tls().server_name &&
             *context.tls().server_name == "policy.example";
    })
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::block;
    });

policy.responses()
    .at_message_complete()
    .when([](const ntl::net::http::inspection_context_view &context) {
      return context.request().headers.first("x-inspect-response") == "1" &&
             context.response() && context.response()->status >= 400;
    })
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::drop_flow;
    });
```

`context.headers()` selects the headers for the current direction;
`context.request()` remains available while inspecting its response. Method,
scheme, authority, path, query, headers, trailers, bounded body, endpoint,
process/application identity, flow direction/identifier, SNI, and ALPN are
therefore independent policy inputs. `body_chunk()` is a callback-lifetime
view and must not be retained.

Custom adapters use `inspection_context_view::for_request(...)` and
`inspection_context_view::for_response(...)`; direction is derived by the selected
factory and a response context cannot be created without a response object.

## HTTP/2 connection and session adapter

`<ntl/net/http2/proxy_connection>` owns the protocol state for one connection:
the two directional HPACK contexts, request/response exchanges, continuation
assembly, staged policy, send windows, SETTINGS, RST_STREAM, and GOAWAY. The
transport-neutral state can be driven frame by frame, while
`<ntl/net/http2/proxy_session>` supplies the normal asynchronous byte-stream
loop:

```cpp
auto connection = std::make_shared<ntl::net::http2::proxy_connection>(
    policy, decoders, encoders, metadata,
    ntl::net::http2::inspection_observer(observer));

co_await ntl::net::http2::run_proxy_session(
    downstream_tls, upstream_tls, connection);
```

The two directions start together, the first EOF, failure, or `drop_flow`
cancels the peer direction, and both children are joined before the operation
returns. No detached relay thread or blocking `task.get()` is required.
The overload above deliberately rejects every CONNECT request, both classic
authority-form CONNECT and Extended CONNECT, before its HEADERS are written to
the origin. A product must pass a tunnel handler
whose `admit(stream_id, request)` returns `inspect` or `passthrough`; unchanged
carriage is therefore an explicit policy decision, never the absence of an
adapter. Rejected admission produces a bounded stream-local 403 response.

The HTTP/1 proxy applies the equivalent pre-origin contract to CONNECT and
Upgrade. Its handler receives `http1_tunnel_offer_view` first and must return
`http1_tunnel_disposition::inspect` or `passthrough`; absence of an admission
method means reject. The eventual 101 or successful CONNECT response can only
enter the relay admitted for that same offer.

Complete 1xx responses run through the same transform, staged decision, and
observer path as final responses, but retain the exchange for the eventual
final response. Associated requests are held once in a shared connection-local
store with both stream-count and aggregate-byte limits, so processing response
frames does not copy a request body for every frame.
Decoded HTTP/2 and HTTP/3 field names are checked for the wire-required
lowercase form before they enter the semantic header collection; malformed
uppercase names are rejected rather than normalized. Duplicate pseudo-fields
remain duplicates even when the first value is empty, `Host` cannot introduce
a second authority alongside `:authority`, and protocol-invalid status 101 is
rejected in both HTTP/2 and HTTP/3.

Redirected TLS inspection also binds the transformed `:authority` to the SNI
that selected the single origin connection before forwarding. Matching accepts
ASCII DNS case differences, one trailing root dot, and implicit or explicit
port 443; missing SNI, another host, another port, an unbracketed IPv6 literal,
or an ordinary `Host` alongside `:authority` fails closed. Set
`require_http2_server_name_authority_binding` to `false` only when a separate
explicit origin-coalescing admission policy enforces the allowed authority set.
HTTP/1 uses the same shared authority parser and the corresponding
`require_http1_server_name_authority_binding` opt-out contract.

`<ntl/net/http2/websocket_tunnel>` is the optional RFC 8441 handler: it
validates Extended CONNECT negotiation, reconstructs WebSocket messages across
arbitrary DATA splits, applies bounded policy and `permessage-deflate`, and
restores client masking. Unknown Extended CONNECT protocols are an explicit
block-or-passthrough choice rather than an accidental opaque bypass. The
WebSocket handler rejects classic CONNECT because it does not provide a raw
byte-tunnel contract for that protocol.

## HTTP/3 connection adapter

`<ntl/net/http3/proxy_connection>` is the owning server-side adapter for one
HTTP/3 connection. It handles SETTINGS/control streams, bounded static and
dynamic QPACK (including blocked-stream resume and decoder acknowledgement),
request/trailer assembly, content decoding and re-encoding, semantic policy,
response framing, GOAWAY, and WebTransport routing. Product code supplies the
same `inspection_policy` used by HTTP/1.1 and HTTP/2. Transforms run first;
policy then observes `headers`, `body_chunk`, and `message_complete` in that
order, including transformed method, path, query, headers, and body.

The core origin boundary is completion based:

```cpp
class product_origin final
    : public ntl::net::http3::async_origin_transport {
public:
  ntl::status submit(
      std::uint64_t exchange_id,
      ntl::net::http3::origin_request request,
      ntl::net::http3::origin_completion completion) noexcept override {
    return queue_.submit(exchange_id, std::move(request),
                         std::move(completion));
  }

  void cancel(std::uint64_t exchange_id) noexcept override {
    queue_.cancel(exchange_id);
  }

private:
  bounded_origin_queue queue_;
};

ntl::net::http3::proxy_connection connection(
    quic_backend, origin, policy, decoders, encoders, metadata, observer);
```

Each request stream is submitted independently. A slow origin therefore does
not block another stream, and completions may arrive in any stream order.
Backend callbacks and completions are serialized inside the connection. Reset,
drain, stop, and close cancel pending exchanges; a racing late or duplicate
completion is ignored. The connection owns its origin and policy dependencies
and retains the QUIC backend while work is active. Destroying an original
facade or calling `close()` from a callback cannot invalidate in-flight state.

`immediate_origin_transport_adapter` explicitly wraps the older blocking
`origin_transport`. It is useful for migration and deterministic fixtures, but
the submitting callback remains blocked until that origin call returns, so it
does not preserve cross-stream HTTP/3 concurrency.

The adapters preserve each protocol's authority boundary: HTTP/2 and HTTP/3
flow-control credit stays with the connection backend, WebSocket client output
is re-masked, gRPC compressed messages are decoded and re-encoded with the
negotiated coding, and WebTransport flow-control capsules are not mutable
content.

`<ntl/net/http3/webtransport_session>` binds the WebTransport semantic layer
to a real QUIC backend: it exchanges draft-16 SETTINGS, emits/accepts static
QPACK Extended CONNECT HEADERS, writes session stream prefixes, and sends HTTP
Datagrams. A move-only stream authority supports multiple writes and either FIN
or a mapped 32-bit application reset; the reset keeps the session prefix
reliable with MsQuic's preview reliable-offset API. The raw MsQuic backend
advertises WebTransport only when reliable-reset-at and QUIC Datagrams are
available and negotiated.

## Managed HTTP/3 client

`<ntl/net/http3/msh3_client>` provides a bounded synchronous user-mode client for
the optional msh3/MsQuic backend. `system_trust_client` uses normal Windows
trust. `private_ca_client` instead validates the peer against one
application-owned CA, the requested DNS name, and server-auth EKU in a private
in-memory chain engine. It never writes that CA to a Windows or browser trust
store and has no certificate-error bypass option.

The request keeps logical origin identity separate from its optional physical
`peer_endpoint`. A managed inspection client can therefore connect to an
explicit loopback proxy while retaining the original SNI and HTTP/3
`:authority`. This is application integration, not transparent interception
of an unrelated browser.

## Browser example boundary

[`browser-https-inspection`](../../examples/wfp/user/browser-https-inspection)
is the complete end-to-end example for IPv4/IPv6 TCP, Schannel TLS,
HTTP/1.1 HTML and WebSocket with `permessage-deflate`, and multiplexed HTTP/2
HTML. Its common bounded decoder registry handles gzip, deflate, and Brotli.
For HTTP/2 it negotiates the origin first, mirrors the selected ALPN to the
browser, and delegates the full connection loop to the reusable adapter. The
adapter keeps independent HPACK state per direction, bounds concurrent stream
and body state, validates control frames, accounts connection/stream windows,
and returns source credit only after transformed data is retained or written.

[`http3-inspection`](../../examples/wfp/user/http3-inspection) demonstrates the
QUIC provider boundary, arbitrary stream splits, static QPACK, and Brotli
HTML. The browser example's application-scoped WFP policy blocks IPv4/IPv6
UDP port 443 so an unmodified stock browser uses the inspectable TCP path.
Its separate managed-client companion uses an explicit loopback endpoint and
application-owned trust to exercise real downstream HTTP/3 without WFP or a
browser setting change. Unsupported content coding and configured frame,
header, stream, or body limits fail closed.
