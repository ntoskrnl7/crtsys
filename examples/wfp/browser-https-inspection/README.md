# WFP browser HTTPS inspection

[Korean walkthrough](./README.ko-KR.md)

This sample is the browser-facing composition of NTL's WFP, TLS, HTTP/1.1,
HTTP/2, HTTP/3, WebSocket, and content-decoding components. It is a portable
example: no machine path, public host, certificate, or browser profile is
compiled into the driver or application.

While the dynamic policy is active, the driver scopes interception to one
browser executable:

- IPv4 and IPv6 TCP port 443 is redirected to the local Schannel proxy.
- IPv4 and IPv6 UDP port 443 is blocked by default so an unchanged Chromium
  browser uses its inspected TCP fallback.

The browser is launched with an isolated profile, but without a switch that
disables QUIC, forces a protocol, ignores all certificate errors, or changes
ECH policy. The wrapper temporarily trusts the example inspection CA and
removes it on exit.

For TCP, the application recovers the original destination, reads a bounded
ClientHello, issues a short-lived SNI-specific leaf, terminates browser TLS,
and creates a normally validated origin TLS connection. It inspects bounded
HTTP/1.1, multiplexed HTTP/2, and HTTP/1.1 WebSocket traffic.

The companion managed-client path is deliberately separate from the WFP
browser policy. The NTL client connects to an explicit loopback inspection
endpoint while retaining the original SNI and `:authority`. It validates the
endpoint against one application-owned CA in memory, without writing that CA
to a Windows or browser trust store. The downstream connection is real
QUIC/TLS 1.3 and HTTP/3.

When that managed client is selected by the optional WFP UDP/443 redirect
fixture, its filter explicitly omits WFP's original-destination context. The
managed protocol already carries the original SNI and `:authority`, and the
HTTP/3 proxy does not query that context. Ordinary browser TCP redirect keeps
the context because its accepted socket must recover the original endpoint.

The managed proxy prefers HTTP/3 to the origin. If external QUIC fails with a
transport, connection, or timeout error, it can retry the origin over normally
validated TLS/TCP. The actual upstream protocol (`h3`, `h2`, or `http/1.1`) is
recorded per request; certificate, mTLS, and request-validation failures never
trigger that fallback.

A deterministic acceptance path removes external UDP/443 availability from
that equation. `Start-ControlledHttp3EndToEnd.ps1` runs a real loopback-only
`NTL client -- H3/TLS 1.3 --> inspection proxy -- H3/TLS 1.3 --> controlled
origin` topology. The two private CAs are validated in memory and are never
installed. The test covers gzip, zlib `deflate`, Brotli, wrong-CA and
wrong-host rejection, an upstream body bound, concurrent requests, and clean
drain without a driver, browser, DNS dependency, or reboot.

`events.log` records the host, negotiated protocol, status, content type,
encoding, and decoded body size. Captured `.html` files contain server
response bodies, not a post-JavaScript DOM snapshot. Request headers and
cookies are not logged.

## Source layout

- `main.cpp`: command-line parsing only
- `browser_runtime.*`: listeners, WFP lifetime, a shared IOCP connection-task
  registry, certificates, and drained shutdown
- `browser_policy.*`: application-scoped TCP and UDP WFP filters
- `browser_proxy.*`: TCP ClientHello, Schannel, and ALPN protocol routing
- `http1_inspection.*`: bounded HTTP/1.1 and WebSocket relay
- `http1_inspection_support.*`: testable request rewriting, content decoding,
  and WebSocket extension negotiation
- `http2_inspection.*`: HTTP/2 frames, HPACK, and stream policy
- `http3_inspection.*`: bounded HTTP/3 response inspection policy
- `http3_controlled.cpp`: deterministic H3 origin and H3 upstream acceptance
- `http3_live_proxy.*`: msh3 server and WinHTTP origin adapters around the
  generic inspection proxy
- `http3_origin.*`: validated origin transport with strict-H3 and
  transport-fallback policies
- `bidirectional_relay.hpp`: structured asynchronous relay completion,
  peer cancellation, and drain
- `browser_log.*`: event and HTML output

The generic parts are under `include/ntl`: `http3_backend`,
`http3_msh3_backend`, `http3_msh3_client`, `http3_inspection_proxy`,
`http3_standard_inspection_proxy`, `http3_qpack`, `http_datagram`,
`http_extended_connect`, `webtransport_http3`, and `tls_inspection_frontend`.
The HTTP/3 inspection proxy validates requests, binds SNI to `:authority`,
enforces bounded decoding, invokes typed request/response policy, and gives
the origin adapter only validated requests.

## Build and run

```powershell
cmake -S examples\wfp\browser-https-inspection `
      -B artifacts\examples\wfp-browser-https-inspection -A x64
cmake --build artifacts\examples\wfp-browser-https-inspection `
      --config Release
```

The application has a product-shaped WFP mode, an explicit managed endpoint,
and a separate controlled browser transport diagnostic:

```text
crtsys_wfp_browser_https_inspection_app.exe <browser.exe> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --managed-http3-proxy <listen-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --controlled-http3-e2e <proxy-port> <origin-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --http3-spki-proxy <server-name> <listen-port> <log-directory> <duration-seconds>
crtsys_ntl_managed_http3_client.exe <https-url> <output-file> [<inspection-port> <inspection-ca.cer>]
```

Prepare the portable package as described in the
[live runtime guide](../../../test/wfp/runtime/https-live/README.md). In an
elevated PowerShell inside that package:

```powershell
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Urls @('https://www.google.com/') `
    -RequireQuicBlockedFallback `
    -LogDirectory (Join-Path (Get-Location) 'browser-log') `
    -DurationSeconds 90
```

`-RequireQuicBlockedFallback` is an automation assertion and does not alter
the browser. It verifies the live WFP objects and exact filter conditions,
requires a matching kernel UDP/443 classify and block, rejects target-host
direct QUIC reachability from Edge NetLog, and requires inspected HTML over
TCP. A run with no QUIC classify is reported as inconclusive rather than
PASS. The log directory retains bounded policy, kernel telemetry, and NetLog
verdict files.

The managed path requires no driver, browser launch, browser flag, or trust
store update:

```powershell
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Url 'https://www.google.com/' `
    -LogDirectory (Join-Path (Get-Location) 'managed-http3-log')
```

This wrapper starts the explicit loopback endpoint, passes its ephemeral CA
only to `crtsys_ntl_managed_http3_client.exe`, verifies downstream `h3`, and
checks that an HTML response was inspected.

Run both H3 legs without an external network:

```powershell
.\Start-ControlledHttp3EndToEnd.ps1 `
    -PackageRoot (Get-Location).Path `
    -Concurrency 8
```

The driverless package and VM state checks are documented in
`test/wfp/runtime/https-live/CONTROLLED-HTTP3-README.ko-KR.md`.

`Start-BrowserHttp3SpkiDiagnostic.ps1` remains a deliberately isolated
transport acceptance tool. It maps test traffic to loopback and supplies one
exact ephemeral SPKI exception to its disposable browser process. It is not
the normal WFP path and is not evidence of transparent interception.

## Bounds and protocol coverage

The TCP path covers TLS 1.2/1.3 as negotiated by Schannel, HTTP/1.1 HTML,
RFC 6455 WebSocket with validated `permessage-deflate`, and multiplexed
HTTP/2 HTML. HTTP/2 keeps independent bounded HPACK state in each direction
and relays flow-control frames unchanged.

HTTP/1.1 and HTTP/2 share bounded gzip, zlib `deflate`, and Brotli content
decoders. Coding depth, input/output size, expansion ratio, checksum,
truncation, and connection limits fail closed.

The NTL HTTP/3 layer contains:

- fragmented frame reassembly;
- bounded dynamic RFC 9204 QPACK, including encoder-stream updates, blocked
  stream resume, acknowledgements, and cancellation;
- RFC 9297 HTTP Datagrams and Capsule Protocol framing;
- HTTP/2 and HTTP/3 extended CONNECT validation; and
- a bounded WebTransport-over-HTTP/3 draft-16 session and stream parser.

The pinned msh3 server backend exposes decoded ordinary request/response
callbacks, but not raw bidirectional/unidirectional streams or QUIC Datagram
callbacks. Its capabilities therefore report extended CONNECT, HTTP
Datagrams, and WebTransport as unavailable. Those NTL protocol components
become live only with a QUIC backend that exposes the required stream and
datagram primitives; the example never claims otherwise.

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

The sample accepts visible SNI, port 443, ordinary HTTP methods, and configured
bounds. Arbitrary ECH, pinning bypass, unmanaged client-certificate choice,
non-443 origins, transparent HTTP/3 MITM of an unmodified stock browser, and
live WebTransport on the pinned msh3 backend are outside the demonstrated
runtime.
