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

`<ntl/net/http3/standard_inspection_proxy>` owns and registers the standard gzip,
zlib `deflate`, and Brotli decoders; targets using it link
`crtsys_ntl_content_codecs`. Use the lower-level `inspection_proxy` when the
application supplies its own decoder registry. Origin and policy objects are
non-owning and must outlive the proxy. A concurrent server requires
thread-safe origin and policy implementations.

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
stages. The browser example composes the HTTP/2 frame, HPACK, content-decoder,
and WebSocket permessage-deflate layers into a transparent multiplexed relay,
with bounded stream/body state while peer SETTINGS and flow control frames
remain end-to-end. See
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
