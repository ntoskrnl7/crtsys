# Bounded content inspection and framing

[Back to NTL documentation](./README.md)

The inspection surface separates three questions that native packet APIs tend
to mix:

1. **What is complete?** UDP supplies one datagram; TCP needs an
   application-selected framer.
2. **What bytes are inspected?** `content_view` is an immutable contiguous or
   fragmented view with bounded binary prefix and containment operations.
3. **What may policy decide?** `inspection::verdict` is typed; the WFP adapter,
   not application policy, owns native block, absorb, defer, and injection
   actions.

## UDP and TCP do not have the same boundary

`udp_datagram_view` represents one validated UDP payload. A UDP send maps to
one datagram even when its storage spans multiple MDLs. IP fragmentation and
network delivery are separate concerns handled below the transport-layer WFP
view used by the sample.

TCP is an ordered byte stream. One receive can contain half a message, one
message, or several messages. `async_framed_stream<Framer>` retains partial
and over-read bytes and returns only a complete owning `framed_message`:

```cpp
ntl::net::async_framed_stream messages(
    socket,
    ntl::net::framing::u32_be_length_prefix{64 * 1024},
    ntl::net::framing::frame_limits{64 * 1024 + 4});

auto message = co_await messages.read_frame();
inspect(ntl::net::inspection::content_view(message.content()));
```

The four-byte prefix above is not a TCP header or TCP standard, and it is not
a fixed message length. It is one example application protocol whose prefix
contains a big-endian, per-message payload length. The framer first waits for
the prefix, reads that message's length, enforces the configured maximum, and
then waits for exactly that complete frame. Bytes belonging to the following
message remain buffered.

Built-in framing contracts cover fixed size, big-endian 16/32-bit length
prefixes, and delimiters. A protocol with flags, optional fields, varints, or
content-dependent length supplies a stateful `noexcept` framer:

```cpp
struct my_protocol_framer {
  ntl::net::framing::frame_probe
  probe(ntl::net::scatter_view available) noexcept {
    // Validate the currently available header.
    // Return need_more(required_total), complete(...), or malformed(status).
  }
};
```

`frame_limits` is applied independently of the custom parser, so a buggy or
hostile length cannot grow the receive buffer without a bound. One
`async_framed_stream` permits one active reader, reports clean EOF before a
complete frame as an error, and uses the owning socket's cancellation path.

## Content policy

`content_view` never claims that bytes are text or a protocol structure. It
provides binary-safe access; the application chooses the parser:

```cpp
ntl::net::inspection::verdict policy(
    const ntl::net::inspection::udp_datagram_view &datagram) {
  const auto forbidden = datagram.payload().contains("BLOCKME");
  if (!forbidden)
    return ntl::net::inspection::verdict::block;
  return *forbidden ? ntl::net::inspection::verdict::block
                    : ntl::net::inspection::verdict::permit;
}
```

For a structured protocol, parse fixed fields with a cursor, validate
content-selected lengths before allocating, wait for the framer to complete
the whole message, and only then invoke policy. `auto data` in application
code is normally an owning `framed_message`, a `udp_datagram_view`, or a
caller-defined parsed value—not an unspecified native packet.

The enforcement examples are transport-specific. The
[`udp-content-filter` sample](../../examples/wfp/udp-content-filter) absorbs
one complete datagram and only reinjects its retained clone after a typed
`permit`. The
[`tcp-content-filter` sample](../../examples/wfp/tcp-content-filter) selects
a u32-big-endian length-prefix application protocol, asks WFP for enough
stream bytes to complete one frame, defers that stream, and resumes exactly
that frame after the user coroutine permits it. A TCP block drops the whole
flow rather than deleting arbitrary bytes. Missing, late, malformed, or
over-quota responses fail closed.

## Compression is bounded and extensible

`<ntl/net/inspection/content_decoder>` supplies a bounded output sink, runtime decoder
contract, and normalized content-coding registry. The optional
`NtlContentCodecs.cmake` backend pins zlib and Brotli and
`<ntl/net/inspection/standard_content_decoders>` registers HTTP `gzip`, strict RFC 1950
`deflate`, and `br`. An application can add Zstandard, dictionary-based, or
proprietary formats through the same fresh-decoder factory. The complete
`Content-Encoding` chain is decoded in reverse application order:

```cpp
ntl::net::inspection::content_decoder_registry decoders;
ntl::net::inspection::register_standard_content_decoders(decoders);

auto decoded = ntl::net::inspection::decode_content_encoding(
    decoders, encoded_message, "gzip, br",
    {.maximum_encoded_size = 1024 * 1024,
     .maximum_decoded_size = 4 * 1024 * 1024,
     .maximum_expansion_ratio = 32});
if (!decoded)
  return ntl::net::inspection::verdict::block;
```

Framing must establish the compressed unit required by that format.
Connection-wide compression keeps one decoder state per connection rather
than using the per-message convenience function. Encoded input, decoded
output, expansion ratio, CPU time, nesting, and dictionary selection all need
explicit limits. A missing coding adapter returns `STATUS_NOT_SUPPORTED`; it
does not reinterpret encoded bytes as plaintext.

## One transform policy for HTTP/1.1, HTTP/2, and HTTP/3

`<ntl/net/http/transform>` presents a complete decoded request or response to
application policy. TLS records, HTTP/1 chunks, HTTP/2 frame boundaries,
HPACK/QPACK state, and transport flow-control authority do not leak into that
policy:

```cpp
ntl::net::http::transform_pipeline pipeline;

pipeline.requests().transform(
    [](ntl::net::http::request_message &request) {
      request.headers.set("x-inspected-by", "ntl");
      return ntl::net::http::rewrite_result::headers_changed();
    });

pipeline.responses()
    .html()
    .transform(
        [](const ntl::net::http::request_message &,
           ntl::net::http::response_message &response) {
          std::string html(
              reinterpret_cast<const char *>(response.body.data()),
              response.body.size());
          html.append("<!-- inspected -->");
          return ntl::net::http::rewrite_result::replace_text(
              std::move(html));
        });
```

The callback body is the complete bounded content-decoded HTTP message body,
not one TCP packet or one HTTP/2 DATA frame. A rule returns `unchanged`,
`headers_changed`, a replacement body, `block`, `drop`, or an immediate
semantic response. The default failure policy is fail-closed. An explicitly
selected `forward_original` policy restores the whole pre-callback message;
it never forwards a partially mutated object.

Configure a pipeline before concurrent use. Callback objects are invoked as
immutable objects; any shared state reached by a callback must provide its own
synchronization. Ordinary fields and trailers are validated separately.
Control characters, pseudo-fields in trailers, hop-by-hop fields, and
framing fields such as trailer `Content-Length` are rejected before output.

`<ntl/net/inspection/standard_content_encoders>` complements the decoder
registry with bounded gzip, zlib `deflate`, and Brotli output. When a body is
changed with `transformed_body_coding::preserve`, the adapter reapplies the
original `Content-Encoding` chain in sender order, updates `Content-Length`,
and removes invalidated response validators such as `ETag`, `Digest`, and
`Content-MD5`. `identity` explicitly removes content coding.

The wire adapters have distinct transport responsibilities:

- `<ntl/net/http/http1_transform>` validates framing, decodes chunked
  transfer coding, applies policy, and serializes a new HTTP/1.1 message.
- `<ntl/net/http2/transform>` maintains independent stateful HPACK decoders
  per connection direction, emits stateless HPACK, reconstructs
  HEADERS/CONTINUATION/DATA, correlates multiplexed requests and responses,
  and reports received flow-controlled bytes to the transport adapter.
- `<ntl/net/http3/standard_inspection_proxy>` delegates QUIC streams, QPACK,
  and flow control to the HTTP/3 backend, while applying the same transform
  pipeline before origin forwarding and before the downstream response.

All adapters validate header names, forbidden hop-by-hop fields, pseudo-field
shape, content length, header/body quotas, coding depth, and re-encoded size.
HEAD, 1xx, 204, and 304 responses retain legal representation metadata such
as `Content-Length` and `Content-Encoding` without trying to decode or emit a
message body. A policy cannot attach body bytes or trailers to those
responses.
HTTP/2 connection adapters must serialize concurrent writes, replenish only
bytes they have retained, and obey the peer's connection and stream send
windows. The browser HTTPS sample demonstrates that complete adapter.

## Bounded asynchronous and streaming policy

`<ntl/net/http/async_transform>` keeps protocol parsing synchronous but moves
potentially slow application policy onto a fixed worker pool. The queue,
concurrency, deadline, cancellation, and fail-open/fail-closed behavior are
all explicit:

```cpp
ntl::net::http::async_transform_pipeline policy(
    {}, {.maximum_concurrency = 8,
         .maximum_queue_depth = 1024,
         .timeout = std::chrono::milliseconds(250)});

policy.responses().html().transform(
    [](const auto&, auto& response, const auto& context) {
      if (context.cancellation_requested())
        return ntl::net::http::rewrite_result::block();
      return decide_and_rewrite(response);
    });

auto outcome = co_await policy.apply(request, response, stop_token);
```

Configuration freezes on the first `apply`. A deadline or external stop
requests cancellation from a running callback and completes only after that
callback returns, so neither the pipeline nor its message can be destroyed
while policy code still uses it. A queued operation can be cancelled
immediately. `statistics()` reports submitted, completed, cancelled,
timed-out, overloaded, queued, and running operations.

`<ntl/net/http/stream_transform>` is for bodies that should not be retained as
one complete allocation. It gives policy owned output per decoded plaintext
chunk and tracks per-chunk, whole-stream, and expansion bounds:

```cpp
ntl::net::http::stream_transform_pipeline body_policy;
body_policy.chunks().transform(
    [](const auto&, const ntl::net::http::stream_chunk& chunk) {
      return rewrite_chunk(chunk.bytes, chunk.input_offset, chunk.final);
    });

ntl::net::inspection::content_decoder_registry decoders;
ntl::net::inspection::content_encoder_registry encoders;
ntl::net::inspection::register_standard_content_decoders(decoders);
ntl::net::inspection::register_standard_content_encoders(encoders);

body_policy.prepare_headers(request, response);
auto body = body_policy.open(request, response, decoders, encoders);
if (!body)
  block();
auto output = body.consume(input_chunk, end_of_message);
```

Stateful application framing must be created per HTTP message. This keeps two
multiplexed HTTP/2 or HTTP/3 streams from sharing a partial gRPC record (or a
custom protocol parser):

```cpp
body_policy.chunks()
    .when(is_grpc_message)
    .transform_session([&](const auto& context) {
      return make_grpc_chunk_transformer(context);
    });
```

`<ntl/net/http/http1_stream_transform>` accepts arbitrary TLS plaintext splits,
buffers only bounded framing state, validates fixed-length, chunked, trailers,
and authenticated close-delimited completion, and emits chunked HTTP/1.1
output. `<ntl/net/http2/stream_transform>` owns independent HPACK and message
state per stream, reserializes HEADERS without dynamic-table coupling, emits
DATA immediately, and reports the precise retained byte count for flow-control
credit. `<ntl/net/http3/stream_transform>` consumes decoded QPACK HEADERS, DATA,
and QUIC FIN events; its `streaming_inspection_sink` plugs directly into
`connection_inspector`, while the caller retains QUIC stream-ID mapping and
write scheduling.

`prepare_headers` removes stale length and validators.
`<ntl/net/inspection/content_stream>` owns one incremental codec chain per HTTP
message. It decodes `gzip`, RFC 1950 `deflate`, `br`, or a registered coding
chain across arbitrary input splits, invokes policy on plaintext chunks, and
re-encodes the same chain without resetting compression state. Input, decoded,
transformed, encoded, per-stage, expansion-ratio, and coding-depth bounds are
independent. Final input verifies checksums and stream termination. Because
emitted bytes cannot be recalled, streaming transform does not permit the
complete-message `forward_original` failure mode. A late block resets the H2/H3
stream (or closes the H1 connection); use the complete-message adapters when a
verdict must be atomic before the first body byte is forwarded.

## WebSocket, gRPC, and WebTransport transforms

The same separation extends past ordinary HTTP messages:

- `<ntl/net/websocket/transform>` assembles fragmented RFC 6455 messages,
  validates UTF-8, decodes and re-encodes negotiated `permessage-deflate`,
  enforces client masking, and fragments bounded rewritten output.
- `<ntl/net/grpc/transform>` accepts arbitrary HTTP/2 or HTTP/3 DATA splits,
  extracts complete five-byte-prefixed gRPC messages, applies the negotiated
  `grpc-encoding`, invokes semantic policy, and emits a valid message stream.
- `<ntl/net/http3/webtransport_transform>` applies per-session policy to
  reliable streams, unreliable datagrams, and capsules while sharing the
  WebTransport stream/data quotas. Authority-bearing flow-control capsules
  may be inspected but not rewritten by content policy.
- `<ntl/net/http3/webtransport_session>` supplies the actual draft-16 HTTP/3
  SETTINGS, static-QPACK Extended CONNECT request/response, session stream
  prefixes, HTTP Datagrams, and backend writes. Its move-only outbound stream
  authority supports repeated bounded writes, FIN, and application resets.
  Resets map the 32-bit WebTransport application error into the registered
  HTTP/3 range and set MsQuic's reliable offset to the complete session prefix
  before aborting the send side. The MsQuic adapter exposes this only when
  QUIC Datagrams and reliable-reset-at were both negotiated.

These adapters do not infer protobuf schemas, WebSocket subprotocols, or
WebTransport application formats. Applications layer their schema parser on
the complete bounded semantic payload.

## Reusable HTTP/3 inspection composition

`<ntl/net/http3/inspection_proxy>` is the transport-neutral policy layer between
an HTTP/3 server backend and an origin transport. The backend owns QUIC,
TLS 1.3, QPACK, and stream lifetimes. The proxy owns the rules that should not
be reimplemented by every application:

- pseudo-header ordering, uniqueness, and required fields;
- HTTPS SNI-to-`:authority` binding;
- hop-by-hop header rejection and exact `Content-Length` validation;
- request, response, header, decoded-body, expansion-ratio, and coding-depth
  limits;
- request and response content decoding before policy; and
- typed `permit`, `block`, and `drop_flow` decisions with fail-closed errors.

The origin receives only a validated `origin_request`. Policy sees an immutable
decoded-body view, while a permitted response retains its original encoded
wire body:

```cpp
auto origin = ntl::net::http3::make_origin_transport(
    [](const ntl::net::http3::origin_request &request) noexcept
        -> ntl::result<ntl::net::http3::origin_response> {
      return send_to_origin_over_h3(request);
    });

auto policy = ntl::net::http3::make_inspection_policy(
    [](const ntl::net::http3::request_view &) noexcept {
      return ntl::net::inspection::verdict::permit;
    },
    [](const ntl::net::http3::response_view &response) noexcept {
      return contains_forbidden_content(response.decoded_body)
                 ? ntl::net::inspection::verdict::block
                 : ntl::net::inspection::verdict::permit;
    });

ntl::net::http3::standard_inspection_proxy proxy(origin, policy);
auto response = proxy.forward(std::move(decoded_http3_request));
```

`<ntl/net/http3/standard_inspection_proxy>` owns and registers the standard
gzip, zlib `deflate`, and Brotli decoders. Its transform-pipeline overload
also registers their matching encoders. Targets that transform compressed
content link `crtsys_ntl_content_codecs`; that target carries both decoder
and encoder backends. Use the lower-level
`inspection_proxy` when the application supplies its own registries. Origin,
legacy inspection policy, and transform objects are non-owning and must
outlive the proxy. A concurrent server requires thread-safe origin and policy
implementations.

## TLS and HTTPS boundary

WFP stream data for TLS is ciphertext. A codec or coroutine can reassemble
TLS records, but it cannot derive HTTP plaintext without the session keys and
TLS state. Plaintext policy therefore belongs at one of these boundaries:

- inside an endpoint that already owns the TLS session;
- after an application explicitly supplies authorized key material; or
- in a separately designed TLS termination proxy with certificate,
  identity, trust, bypass, update, and failure policies.

`<ntl/net/tls/stream>` provides that user-mode Schannel transport boundary. After
TLS termination, `<ntl/net/tls/framed_stream>` retains decrypted suffix bytes and
feeds complete caller-defined messages through the same `content_view` and
typed verdict API. `<ntl/net/http/http1_framing>` supplies bounded
`Content-Length`/`chunked` HTTP/1.x boundaries. `<ntl/net/websocket/framing>`
handles RFC 6455 frames and fragmented messages after a validated upgrade.
`<ntl/net/http2/framing>` and `<ntl/net/http3/framing>` provide bounded wire framing.
`<ntl/net/http2/hpack>` adds a complete bounded stateful HPACK decoder.
`<ntl/net/http3/qpack>` adds the bounded zero-dynamic-table QPACK profile, while
dynamic QPACK remains an explicit provider contract. Transfer decoding,
decompression, semantic policy, and QUIC transport remain explicit later
stages. The browser example composes the HTTP/2 frame, HPACK, content
decoder/encoder, and WebSocket permessage-deflate layers into a multiplexed
transforming relay. It buffers only bounded complete messages, replenishes
the retained receive window, obeys the destination send windows, and
serializes the two policy directions onto one TLS writer per endpoint. See
[User-mode Schannel TLS streams](./tls-stream.md) and the
[`tls-inspection-proxy` sample](../../examples/wfp/tls-inspection-proxy).
The long-running browser lifecycle and HTML logging workflow is demonstrated
by
[`browser-https-inspection`](../../examples/wfp/browser-https-inspection).
The decrypted QUIC provider boundary is demonstrated by
[`http3-inspection`](../../examples/wfp/http3-inspection).

The TLS transport is separate from trust deployment policy. NTL can issue
per-host leaves from an application-supplied authorized CA, but it does not
create or install that CA and does not disable peer validation. ALPN chooses
one parser family; an unimplemented family is not guessed as HTTP/1. Confirmed
ECH, pinning, mTLS identity selection, missing compression codecs, and
unavailable QUIC backends are separate outcomes in
`<ntl/net/tls/inspection_policy>`. Raw extension type `0xfe0d` remains only an
observation because GREASE ECH has the same wire shape. Policy defaults fail
closed. See
[HTTP and WebSocket protocol inspection](./protocol-inspection.md).

`<ntl/net/tls/product_policy>` converts those observations plus deployed
capabilities into one executable action: intercept, unchanged ciphertext
tunnel, metadata-only observation, terminate, or block QUIC and wait for the
application's normal TCP retry. It never changes browser settings and never
labels ECH, certificate pinning, or missing mTLS identity as “bypassed.” An
ECH frontend and authorized mTLS identity provider remain product-supplied
capabilities because WFP cannot manufacture their cryptographic authority.

`<ntl/net/tls/product_backend>` turns those product capabilities into a
single auditable selection. It combines the ECH provider, application/host
trust classification, and the cached downstream certificate issuer. A
non-ECH connection returns a Schannel server identity; successfully decrypted
ECH returns the frontend-owned plaintext channel, because the outer TLS/QUIC
state cannot be resumed in Schannel. Confirmed opaque ECH, pinned or unknown
downstream trust, and missing origin mTLS identities are distinct fail-closed
audit events. `audited_origin_client_identity_provider` adds the same bounded
audit contract to an application-supplied mTLS selector.
