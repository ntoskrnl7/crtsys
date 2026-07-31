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
| HTTP/2 | Bounded frame validation, HEADERS/CONTINUATION assembly, complete bounded HPACK decoding, decoded-header/DATA inspection routing | Semantic policy and one decoder/inspector instance per connection direction |
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
ntl::net::http2::connection_inspector inspector(decoder);
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

[`browser-https-inspection`](../../examples/wfp/browser-https-inspection)
is the complete end-to-end example for IPv4/IPv6 TCP, Schannel TLS,
HTTP/1.1 HTML and WebSocket with `permessage-deflate`, and multiplexed HTTP/2
HTML. Its common bounded decoder registry handles gzip, deflate, and Brotli.
For
HTTP/2 it negotiates the origin first, mirrors the selected ALPN to the
browser, validates and transparently relays frames, keeps independent HPACK
state per direction, and bounds concurrent response state and bodies.
SETTINGS and WINDOW_UPDATE remain byte-for-byte end-to-end rather than being
reimplemented by the inspector.

[`http3-inspection`](../../examples/wfp/http3-inspection) demonstrates the
QUIC provider boundary, arbitrary stream splits, static QPACK, and Brotli
HTML. The browser example's application-scoped WFP policy blocks IPv4/IPv6
UDP port 443 so an unmodified stock browser uses the inspectable TCP path.
Its separate managed-client companion uses an explicit loopback endpoint and
application-owned trust to exercise real downstream HTTP/3 without WFP or a
browser setting change. Unsupported content coding and configured frame,
header, stream, or body limits fail closed.
