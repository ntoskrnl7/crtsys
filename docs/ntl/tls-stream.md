# User-mode Schannel TLS streams

[Back to NTL documentation](./README.md)

`<ntl/net/tls/stream>` adds a coroutine TLS transport above
[`async_socket`](./async-socket.md). It is a user-mode API backed by Windows
Schannel. Related headers add bounded ClientHello observation, dynamic server
identities, and application framing. None of them installs WFP policy or
modifies a Windows trust store.

The normal client path uses Windows certificate-chain and host-name
validation. Protocol and cipher-suite selection are left to current Schannel
policy, with strong cryptography requested through `SCH_CREDENTIALS`:

```cpp
auto credentials = ntl::net::tls_credentials::client();
ntl::net::tls_stream tls(socket, credentials);

co_await tls.handshake_client({
    .server_name = L"service.example",
    .application_protocols = {"h2", "http/1.1"},
    .require_application_protocol = true
});

auto protocol = tls.negotiated_application_protocol();
auto received = co_await tls.read_some_borrowed(buffer);
co_await tls.write_all(reply);
co_await tls.shutdown();
```

`tls_credentials` may be shared by several sessions. Each stream retains the
credential and socket states it uses, so their facades and member declaration
order do not control the session lifetime. Every `tls_stream` is one TLS
session over one already-connected `async_socket`.

## Certificate policy

A server supplies a certificate whose private key Schannel can open:

```cpp
auto credentials =
    ntl::net::tls_credentials::server(certificate);
ntl::net::tls_stream tls(socket, credentials);
co_await tls.handshake_server();
```

Applications that already know a server name can use
`tls_server_certificate_policy`. A transparent TLS endpoint instead observes
one bounded ClientHello and selects an owning identity:

```cpp
auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
    authorized_ca,
    {.key_name_prefix = L"product-tls-leaf",
     .rsa_bits = 2048,
     .validity_days = 7,
     .machine_keys = true});
auto identities = std::make_shared<
    ntl::net::cached_tls_server_identity_provider>(
    issuer, 256);

auto accepted = co_await ntl::net::accept_tls(
    accepted_socket, identities);
auto &tls = accepted.stream();
auto sni = accepted.client_hello_ref().server_name();
```

`<ntl/net/tls/client_hello>` accepts fragmented TLS records and handshake
messages, bounds every retained byte, reports SNI, offered ALPN identifiers,
and whether extension type `0xfe0d` is present, and retains all consumed
ciphertext. That extension is an observation only: GREASE ECH has the same
wire shape and cannot be distinguished by this parser. `accept_tls` gives
those exact bytes to Schannel after identity selection; it never reconstructs
or discards part of the TLS stream.

`<ntl/net/tls/certificate>` provides interfaces rather than a global CA:

- `tls_certificate_issuer` is the application injection point;
- `windows_tls_certificate_issuer` signs a short-lived DNS SAN leaf with an
  application-supplied CA whose private key Windows can open;
- `cached_tls_server_identity_provider` is a bounded, synchronized LRU of
  leaf certificate plus reusable Schannel credential; and
- an evicted generated leaf deletes its persisted CNG private key when the
  last identity owner releases it.

Certificate issuance is serialized by the cache to prevent duplicate
same-host creation. Products that use an HSM, remote issuer, asynchronous
approval, or a pre-provisioned store should implement the issuer/provider
interfaces with their own scheduling policy.

Client credentials use normal system validation unless
`manual_peer_validation` is explicitly enabled. Manual validation is
fail-closed and requires a `tls_peer_certificate_policy`:

```cpp
auto credentials = ntl::net::tls_credentials::client({
    .manual_peer_validation = true
});
auto pin = std::make_shared<ntl::net::exact_certificate_policy>(
    expected_certificate);
ntl::net::tls_stream tls(socket, credentials);

co_await tls.handshake_client({
    .server_name = L"fixture.example",
    .certificate_policy = pin
});
```

`exact_certificate_policy` compares the complete DER certificate.
`certificate_authority_policy` builds a private chain engine around one
application-owned CA and performs server-EKU and host-name validation without
writing a trusted-root store. It is intended for controlled clients and
tests. Normal deployed clients should receive an authorized CA through their
administrator and keep ordinary Schannel validation enabled.

The same rule applies to an upstream leg on a managed network that re-signs
HTTPS. Its filtering CA must already be present in the applicable Windows
trusted-root store. `tls_credentials::client()` then validates that chain
normally; if the CA is absent, `SEC_E_UNTRUSTED_ROOT` is the intended
fail-closed result.

Revocation behavior is a separate explicit credential policy. The default
leaves it to the system. Applications that require a specific check can
choose the end certificate, the complete chain, or the chain excluding its
root. Availability exceptions are rejected unless an explicit check was
selected:

```cpp
auto credentials = ntl::net::tls_credentials::client({
    .revocation_check =
        ntl::net::tls_certificate_revocation_check::
            chain_excluding_root,
    .ignore_missing_revocation_information = true,
    .ignore_offline_revocation = true
});
```

The two availability options map to Schannel's documented
`SCH_CRED_IGNORE_NO_REVOCATION_CHECK` and
`SCH_CRED_IGNORE_REVOCATION_OFFLINE` flags. They do not disable chain,
enhanced-key-usage, host-name, expiry, or positive revocation validation.

Handshake options retain custom certificate policies. TLS 1.3 post-handshake
validation can therefore reuse the policy even after the original policy
facade has been released.

An application can supply one explicit client certificate instead of asking
Schannel to choose a default identity:

```cpp
auto client_credentials = ntl::net::tls_credentials::client({
    .certificate = client_certificate
});
```

A server that requires mutual TLS must also provide an authorization policy;
requiring a certificate without a policy is rejected before the handshake:

```cpp
ntl::net::exact_client_certificate_policy authorized(client_certificate);
co_await server_tls.handshake_server({
    .application_protocols = {"http/1.1"},
    .require_client_certificate = true,
    .client_certificate_policy = &authorized
});
```

The certificate choice and authorization policy are application concerns.
The browser inspection example cannot manufacture an origin-specific client
identity and therefore fails closed when an identity is required but no
configured identity provider can supply one.

NTL deliberately has no built-in interception CA, CA-generation shortcut,
silent root installer, exported private key, or validation-disable switch. An
HTTPS inspection product must explicitly own authorization, protected CA
provisioning, client trust deployment, audit and disclosure, bypass lists,
certificate-pinning behavior, and fail-open/fail-closed policy.

## Record and byte-stream behavior

The caller sees decrypted bytes, not TLS records. `tls_stream`:

- retains fragmented and coalesced ciphertext across socket reads;
- preserves Schannel `SECBUFFER_EXTRA` bytes for the next record;
- handles TLS 1.3 post-handshake continuation;
- splits large plaintext writes at Schannel's maximum message size; and
- distinguishes authenticated `close_notify` from a raw transport EOF.

TLS does not create application-message boundaries.
`<ntl/net/tls/framed_stream>` applies the same bounded `Framer` contract as
`async_framed_stream` above decrypted TLS. It retains suffix bytes when one
TLS read contains several messages:

```cpp
ntl::net::tls_framed_stream requests(
    tls,
    ntl::net::http::http1_message_framer{
        ntl::net::http::http1_message_kind::request},
    ntl::net::framing::frame_limits{2 * 1024 * 1024});

auto request = co_await requests.read_frame();
```

`<ntl/net/http/http1_framing>` recognizes bounded HTTP/1.0 and HTTP/1.1
`Content-Length` and final-`chunked` message boundaries. It rejects conflicting
lengths, length plus transfer-encoding, obsolete header folding, oversized
headers/bodies/chunk lines/trailers, and ambiguous ordinary responses without
a self-delimiting body. A caller may explicitly enable close-delimited
responses; they complete only after the underlying TLS stream receives an
authenticated `close_notify`, remain body-size bounded, and are never inferred
from a partial read. Transfer decoding, content decompression, and semantic
HTTP policy remain later stages. A validated HTTP/1.1 WebSocket upgrade can
transfer any already-decrypted suffix through
`release_buffered()`/`append_buffered()` to `<ntl/net/websocket/framing>` without
losing bytes. Negotiated WebSocket `permessage-deflate` is decoded by
`<ntl/net/websocket/permessage_deflate>` without changing the relayed frame.
HTTP/2 uses `<ntl/net/http2/framing>` with the complete bounded
`<ntl/net/http2/hpack>` decoder, one instance per connection direction. HTTP/3
uses `<ntl/net/http3/framing>`, the zero-dynamic-table decoder in
`<ntl/net/http3/qpack>`, or a caller-supplied stateful QPACK decoder above a
separately selected decrypted QUIC-stream backend. HTTP bodies share the
bounded gzip/deflate/Brotli registry in
`<ntl/net/inspection/standard_content_decoders>`.

`tls_stream_limits` bounds retained ciphertext and the size of each underlying
socket receive. The defaults are a 1 MiB maximum and a 16 KiB receive chunk.
The limit is independent of any application-level decoded-size or framing
limit.

## Concurrency, shutdown, and lifetime

One reader and one writer may be active concurrently. A second reader, a
second writer, or a handshake/shutdown race fails instead of sharing mutable
Schannel state accidentally.

The socket, credentials, completion context, application buffers, awaiting
coroutine, and any custom certificate policy must remain alive until their
operations finish. `shutdown()` sends `close_notify` without closing the
underlying socket. Reads may continue afterward to receive the peer's
`close_notify`; the application still owns socket closure and cancellation.

Schannel and Winsock failures surface as `std::system_error`. An unauthenticated
transport EOF during an established TLS read is reported as an error rather
than as a clean TLS shutdown.

## WFP composition

[`tls-inspection-proxy`](../../examples/wfp/user/tls-inspection-proxy) demonstrates
the intended split:

1. a kernel WFP callout forces one selected TCP connection through a local
   proxy;
2. user mode recovers the original destination and redirect records;
3. a bounded ClientHello probe selects a per-SNI server identity;
4. one Schannel session terminates the accepted leg and another validates and
   protects the outbound leg;
5. bounded HTTP/1.1 framing runs on the decrypted byte stream; and
6. [`ntl::net::inspection`](./inspection.md) returns the typed content verdict.

The kernel never receives TLS keys or plaintext. This keeps TLS libraries,
certificate policy, parsers, and potentially blocking product decisions out
of WFP classify callbacks.

[`browser-https-inspection`](../../examples/wfp/user/browser-https-inspection)
demonstrates a browser-scoped long-running workflow that observes an
already-running exact executable path without launching, terminating, or
reconfiguring the browser. It provides bounded HTTP/1.1 and multiplexed HTTP/2
HTML logging, gzip/deflate/Brotli body decoding, negotiated WebSocket
`permessage-deflate`, and transparent end-to-end HTTP/2 flow-control frames.

This is an authorized TCP/TLS interception foundation, not a promise that
every HTTPS client is interceptable. Raw `0xfe0d` presence is not proof of ECH.
Confirmed ECH is inspectable only if an `ech_frontend_provider` successfully
recovers the inner ClientHello. A `downstream_trust_provider` can identify a
known pinned endpoint before interception, but cannot make it accept another
certificate. Origin mutual TLS uses an explicit
`origin_client_identity_provider`; NTL never guesses an identity. The
[`http3-inspection`](../../examples/wfp/user/http3-inspection) example covers the
decrypted QUIC/static-QPACK boundary, while transparent browser HTTP/3 still
needs a product QUIC terminator and any negotiated dynamic QPACK provider.
`<ntl/net/tls/inspection_policy>` represents each missing capability separately
and defaults every exceptional path to fail closed. See
[HTTP and WebSocket protocol inspection](./protocol-inspection.md).
