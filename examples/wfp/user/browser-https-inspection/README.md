# WFP browser HTTPS inspection

[Korean walkthrough](./README.ko-KR.md)

This sample is the browser-facing composition of NTL's WFP, TLS, HTTP/1.1,
HTTP/2, HTTP/3, WebSocket, and content-decoding components. It is a portable
example: no machine path, public host, certificate, or browser profile is
compiled into the driver or application.

While the ephemeral policy is active, the driver scopes interception to one
browser executable:

- IPv4 and IPv6 TCP port 443 is redirected to the local Schannel proxy.
- IPv4 and IPv6 UDP port 443 is blocked by default so an unchanged Chromium
  browser uses its inspected TCP fallback.

The wrapper observes an already-running browser at that exact executable path.
It never launches or terminates the browser, creates or deletes a profile, or
supplies profile, feature, certificate, QUIC, ECH, host-mapping, or logging
arguments. It temporarily trusts the example inspection CA and removes it on
exit.

For TCP, the application recovers the original destination, reads a bounded
ClientHello, issues a short-lived SNI-specific leaf, terminates browser TLS,
and creates a normally validated origin TLS connection. It inspects bounded
HTTP/1.1, multiplexed HTTP/2, HTTP/1.1 Upgrade WebSocket, and HTTP/2 Extended
CONNECT WebSocket traffic.

HTTP/1.1, HTTP/2, and the raw-MsQuic HTTP/3 service use the same semantic
`ntl::net::http::transform_pipeline`. Policy receives complete decoded
messages and can inspect, change headers or bodies, block, drop, or return a
synthetic response. Protocol adapters own chunking, HPACK/QPACK, compression
re-encoding, content length, multiplexing, and flow control.

The shared policy also recognizes the exact `application/grpc` media-type
family and applies a bounded `ntl::net::grpc::message_transform_pipeline` to
complete gRPC messages over HTTP/1.1, HTTP/2, and HTTP/3. A truncated gRPC
envelope fails closed;
look-alike media types such as `application/grpcfoo` are ordinary HTTP. Tunnel
policy is protocol-specific: H2 WebSocket and H3 WebTransport are accepted,
while H2 WebTransport, H3 WebSocket, and ordinary CONNECT fail closed.

The HTTP/3 proxy service is deliberately separate from the browser controller.
It owns the QUIC endpoint and, in its WFP mode, the application-scoped UDP/443
policy. A client keeps the original SNI and `:authority`; private-CA selection
belongs to the client-side integration rather than the WFP driver.

When that managed client is selected by the optional WFP UDP/443 tuple-
translation fixture, FLOW_ESTABLISHED records the original tuple and
DATAGRAM_DATA redirects outbound client datagrams. OUTBOUND_IPPACKET validates
local proxy replies, copies them to fresh bounded NBLs, restores the original
source tuple, and reinjects them through network send. It does not allocate
WFP's connect-redirect context: the managed protocol already carries the
original SNI and `:authority`. Ordinary browser TCP redirect keeps that context
because its accepted socket must recover the original endpoint.

The managed proxy prefers HTTP/3 to the origin. If external QUIC fails with a
transport, connection, or timeout error, it can retry the origin over normally
validated TLS/TCP. The actual upstream protocol (`h3`, `h2`, or `http/1.1`) is
recorded per request; certificate, mTLS, and request-validation failures never
trigger that fallback.

`events.log` records the host, negotiated protocol, status, content type,
encoding, and decoded body size. Captured `.html` files contain server
response bodies, not a post-JavaScript DOM snapshot. Request paths, queries,
headers, cookies, credentials, and request bodies are not written to the
metadata log. Decoded HTML capture is an explicit, content-type-gated output.

## Source layout

- `main.cpp`: real-browser controller command-line parsing only
- `browser_runtime.*`: listeners, WFP lifetime, a shared IOCP connection-task
  registry, certificates, and drained shutdown
- `browser_policy.*`: application-scoped TCP and UDP WFP filters
- `browser_proxy.*`: TCP ClientHello, Schannel, and ALPN protocol routing
- `http1_inspection.*`: bounded HTTP/1.1 and WebSocket relay
- `http1_inspection_support.*`: testable request rewriting, content decoding,
  and WebSocket extension negotiation
- `http2_inspection.*`: example-specific HTTP/2 logging and WebSocket policy
  wired to the reusable NTL connection/session adapters
- `http3_inspection.*`: browser-specific HTTP/3 policy and privacy-preserving
  inspection observer
- `http3_live_proxy.*`: dynamic TLS identity and listener composition around
  the reusable MsQuic server, HTTP/3 connection, and async origin pool
- `http3_origin.*`: validated origin transport with strict-H3 and
  transport-fallback policies
- `http3_proxy_service.*`, `http3_service_main.cpp`: independently hosted H3
  proxy service, WFP policy ownership, and ready/stop lifecycle
- `bidirectional_relay.hpp`: structured asynchronous relay completion,
  peer cancellation, and drain
- `browser_log.*`: event and HTML output

The generic parts are under `include/ntl`: `http_transform`,
`ntl/net/http/inspection_policy`, `ntl/net/http/decision_policy`,
`ntl/net/http/inspection_context_view`, `ntl/net/http/inspection_conditions`,
`http1_proxy_connection`,
`http2_proxy_connection`, `http2_proxy_session`, `http2_websocket_tunnel`,
`content_encoder`,
`standard_content_encoders`, `http3_backend`,
`http3_msh3_client`, `http3_msquic_backend`, `http3_msquic_runtime`,
`http3_msquic_server`, `http3_async_origin_pool`,
`http3_proxy_connection`, `http3_inspection_proxy`,
`http3_standard_inspection_proxy`, `http3_qpack`, `http_datagram`,
`http_extended_connect`, `webtransport_http3`, `webtransport_session`,
`content_stream`, `tls_inspection_frontend`, and `tls_product_backend`.
The HTTP/3 connection validates requests, binds SNI to `:authority`, enforces
bounded decoding, invokes typed request/response policy, and gives the origin
adapter only validated requests. It also owns control streams, SETTINGS,
QPACK, request-stream state, Extended CONNECT, WebTransport, terminal
responses, reset, cancellation, and drained shutdown. The example supplies
only browser policy, privacy logging, certificate selection, and origin choice.

A permit/block decision is not a body-only callback. One
`inspection_context_view` combines method, scheme, authority, path/query,
request/response headers and trailers, decoded content, protocol and stream
identity, PID/application/original-destination/WFP flow metadata, TLS SNI, and
ALPN. The supplied conditions are conveniences, not a closed rule language:

```cpp
using namespace ntl::net::http::condition;

policy.requests()
    .at_headers()
    .when(header_name_starts_with("custom-"))
    .when(any_header([](const auto &header) {
      return header.name.starts_with("custom-") &&
             header.value == "enabled";
    }))
    .when([](const ntl::net::http::inspection_context_view &context) {
      return context.method() == "POST" &&
             context.path() == "/inspect" &&
             context.connection().original_destination &&
             context.connection().original_destination->port == 443;
    })
    .decide(product_policy);
```

Use `header_name_starts_with("custom-")` by itself when only the name prefix
matters. Use `any_header(...)` when the name/value relation is product-defined,
and use the raw `when(inspection_context_view)` extension point to combine HTTP,
TLS, and WFP context. Response rules can select associated request names with
`request_header_name_starts_with` or current response names with
`response_header_name_starts_with`. NTL normalizes field names to lowercase;
the typed prefix helpers normalize the supplied prefix as well.

Rules in the same direction and stage are evaluated in registration order and
the first match is terminal. Put narrow permit rules before a condition-free
block rule for an explicit allow-list; no matching rule permits the stage.

`header_is` and its request/response variants inspect every repeated field;
a benign first value cannot hide a later matching value.

## Build and run

```powershell
cmake -S examples\wfp\user\browser-https-inspection `
      -B artifacts\examples\wfp-browser-https-inspection -A x64
cmake --build artifacts\examples\wfp-browser-https-inspection `
      --config Release
```

The build produces a real-browser controller and a separately hosted HTTP/3
proxy service:

```text
crtsys_wfp_browser_https_inspection_controller.exe <browser.exe> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_http3_proxy_service.exe --managed-http3-proxy <listen-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_http3_proxy_service.exe --wfp-managed-http3-proxy <client.exe> <listen-port> <log-directory> [duration-seconds]
```

Prepare the portable package as described in the
[live runtime guide](../../../../test/wfp/runtime/https-live/README.md). In an
elevated PowerShell inside that package, first open the browser normally and
leave it running. Then start the observer and navigate in that existing window:

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Urls @($inspectionUrl) `
    -RequireQuicBlockedFallback `
    -LogDirectory (Join-Path (Get-Location) 'browser-log') `
    -DurationSeconds 90 `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

`-Urls` names expected captures; it does not navigate the browser.
`-RequireQuicBlockedFallback` is an automation assertion and does not alter
the browser. It verifies application-scoped native IPv4/IPv6 UDP/443 block
filters, cross-checks each printed filter ID against the bounded inventory
from that same run, requires a matching WFP `classify_drop` net event, and
requires fresh inspected HTML over TCP. A run with no matching UDP challenge
is reported as inconclusive rather than PASS. The log directory retains
`wfp-policy-diagnostics.log`, `browser-transport-evidence.json`, proxy logs,
and the captured HTML. Edge NetLog and callout `action_write` counters are not
used as proof.

The service writes `service.ready` only after its endpoint and optional WFP
policy are active. Creating `stop.request` in the same log directory requests
a drained shutdown. Traffic generators, private test origins, and automated
end-to-end assertions live under
`test/wfp/runtime/fixtures/user/browser-https-inspection`; they are not modes
of either product example executable.

## Bounds and protocol coverage

The TCP path covers TLS 1.2/1.3 as negotiated by Schannel, persistent
HTTP/1.1 HTML, multiplexed HTTP/2 HTML, HTTP/1.1 Upgrade WebSocket, and RFC
8441 Extended CONNECT WebSocket. Both WebSocket paths validate and transform
complete RFC 6455 messages and negotiated `permessage-deflate`. HTTP/2 DATA
boundaries are not treated as WebSocket frame boundaries: a bounded stream
framer carries partial WebSocket frames across DATA frames and re-encodes the
transformed message into new DATA frames. Other Extended CONNECT protocols are
blocked by this sample's explicit fail-closed policy rather than silently
treated as WebSocket. The generic HTTP/2 proxy session is fail-closed as well:
it calls the selected handler's admission method for classic and Extended
CONNECT before writing HEADERS to the origin, rejects by default, and carries
opaque DATA only when a real byte-tunnel handler explicitly selects
passthrough.

The HTTP/1 proxy applies the same pre-origin rule to CONNECT and Upgrade.
This sample admits only a validated WebSocket Upgrade for inspection;
ordinary CONNECT and unknown switches receive a local bounded 403 without
being written to the origin.

Before an HTTP/1 or HTTP/2 request is written to its single TLS origin
connection, the adapter binds the transformed authority to that connection's
SNI. DNS case,
a trailing root dot, and implicit or explicit port 443 are canonical matches;
missing SNI, another host/port, or a simultaneous ordinary `Host` field fail
closed.

HTTP/2 keeps independent bounded HPACK state in each direction and re-encodes
transformed headers without dynamic-table coupling. Every ordinary and tunnel
DATA write reserves the peer connection and stream send windows. Source
WINDOW_UPDATE credit is emitted only after the bytes have been retained by a
bounded transformer or written to the destination.

HTTP/1.1, HTTP/2, and HTTP/3 share bounded gzip, zlib `deflate`, and Brotli
content decoders and encoders. Changed bodies may preserve the original
coding chain or explicitly become identity-coded. Coding depth, input/output
size, expansion ratio, checksum, truncation, and connection limits fail
closed. The reusable streaming API retains one incremental codec chain per
message, so arbitrary HTTP chunk/DATA splits do not reset gzip, deflate, or
Brotli state and do not require one complete body allocation.

The sample deliberately uses the complete-message transform for HTML logging
and policies that must decide before forwarding any rewritten body byte. The
library also supplies live adapters in
`ntl/net/http/http1_stream_transform`, `ntl/net/http2/stream_transform`, and
`ntl/net/http3/stream_transform`. Those adapters forward transformed chunks as
soon as framing and codec state permit; a later rejection resets or closes the
stream and cannot retract bytes already emitted. Choose the atomic or live
adapter from the policy's decision boundary rather than from the HTTP version.

The NTL HTTP/3 layer contains:

- fragmented frame reassembly;
- bounded dynamic RFC 9204 QPACK, including encoder-stream updates, blocked
  stream resume, acknowledgements, and cancellation;
- RFC 9297 HTTP Datagrams and Capsule Protocol framing;
- HTTP/2 and HTTP/3 extended CONNECT validation; and
- a bounded WebTransport-over-HTTP/3 draft-16 session, stream, datagram, and
  transform layer.

The service uses `msquic_server` plus `proxy_connection`; it does not implement
SETTINGS, QPACK, request-stream, or WebTransport state in sample code. The
raw MsQuic backend exposes request, bidirectional, unidirectional, Datagram,
and reliable-reset-at events. Its live loopback contract performs TLS 1.3/h3
negotiation, exchanges SETTINGS, completes a QPACK Extended CONNECT, and
transfers a WebTransport bidirectional stream, unidirectional stream, and HTTP
Datagram. It also opens a multi-write stream, maps a 32-bit application error
into the draft HTTP/3 range, and proves the peer receives a reliable-prefix
reset. The pinned msh3 dependency remains only for controlled client/origin
fixtures. WebTransport capability remains false when the required preview
reliable-reset-at API is unavailable.

## Security boundaries

WFP application identity is the executable path, not the disposable profile.
Every process using that executable is in scope while the dynamic filters are
active. Use a dedicated VM and protect captured HTML.

ECH extension presence is not proof of usable ECH because GREASE has the same
wire shape. Arbitrary public ECH cannot be decrypted from WFP metadata or by
Schannel. A product needs the matching ECH private configuration and a TLS
frontend that owns HPKE, inner/outer ClientHello validation, and termination.
Without that frontend, confirmed ECH fails closed.

Certificate pinning cannot be bypassed by NTL. A downstream trust provider can
classify an exact application/host pair before a substitute leaf is issued so
policy can block or bypass; it cannot make a pinned client trust that leaf.

Origin mTLS is supported through an explicit SNI-to-client-certificate
provider on both TCP and HTTP/3 origin paths. The selected certificate must
have an accessible private key. Missing required identity fails closed.

Upstream Chromium's QUIC proof verifier rejects an otherwise valid chain when
`is_issued_by_known_root` is false. Its built-in verifier deliberately
distinguishes a standard root from a locally or enterprise-installed root.
Windows root-store trust and Edge's `CACertificates` enterprise trust policy
can make the chain valid without making that private anchor a QUIC
`known root`. The default therefore blocks UDP 443 and inspects TCP fallback
without changing browser settings.

This is a boundary of the demonstrated WFP-plus-private-CA path, not a claim
that every commercial product is unable to inspect HTTP/3. A product with a
managed client or browser-specific integration can have capabilities that an
ordinary private CA and WFP redirect do not provide. The separate NTL managed
client demonstrates that integration: it owns the endpoint mapping and the
private-CA trust decision inside the application.

The product TLS contracts also provide managed identity selection, an owned
plaintext handoff from an actual ECH frontend, explicit origin mTLS selection,
and bounded audit. They do not supply private ECH configurations or authority
to bypass endpoint pinning.

The sample accepts visible SNI, port 443, ordinary HTTP methods, and configured
bounds. Arbitrary public ECH without a configured frontend, pinning bypass,
unmanaged client-certificate choice,
non-443 origins, transparent HTTP/3 MITM of an unmodified stock browser, and
browser-originated WebTransport through that transparent private-CA path are
outside the demonstrated runtime. The raw MsQuic WebTransport loopback is an
actual transport test, but it is not evidence that an unmodified Chromium
browser accepts an enterprise/private CA for QUIC.
