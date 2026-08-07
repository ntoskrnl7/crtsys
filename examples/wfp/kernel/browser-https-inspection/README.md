# Kernel browser HTTPS inspection

[한국어](./README.ko-KR.md) · [WFP samples](../../README.md)

This is the kernel-data-plane counterpart of the user-mode
[`browser-https-inspection`](../../user/browser-https-inspection/) example.
The driver terminates TLS, parses HTTP, applies bounded transformations and
permit/block policy, connects to a validated origin, and publishes privacy-
bounded capture records.

## Process boundary

The production example and its managed runtime acceptance are separate:

- `crtsys_wfp_kernel_browser_https_inspection.sys` owns the kernel TCP/TLS,
  HTTP/1.1, HTTP/2, QUIC/HTTP/3, WebSocket, WebTransport, content-decoding,
  transform, and capture data planes.
- `crtsys_wfp_kernel_browser_https_inspection_controller.exe` manages the
  driver, identities, and application-scoped WFP policy. In normal mode it
  observes an already-running browser. It never launches a browser, generates
  HTTP traffic, hosts a controlled origin, or prints an acceptance PASS.
- `crtsys_wfp_kernel_browser_https_inspection_acceptance.exe` is built only
  when `BUILD_TESTING=ON`. Its source is under
  `test/wfp/runtime/fixtures/kernel/browser-https-inspection`. It owns the
  deterministic origins, managed clients, traffic generation, evidence gates,
  and PASS result.

The acceptance executable launches the adjacent controller executable in its
control-server mode. A successful versioned named-pipe hello is the ready
contract. Every WFP session and driver IOCTL remains in the controller
process. The acceptance process sends typed commands, runs only controlled
network exchanges, sends an explicit stop command, and waits for the
controller to exit. There is no combined `_app.exe` and no `--self-test`
mode.

## Coverage

| Protocol | Kernel path | Inspected behavior | Runtime evidence |
| --- | --- | --- | --- |
| HTTP/1.1 | WSK + kernel Schannel over app-scoped IPv4/IPv6 TCP redirect | pipelining, permit/block, HTML and bounded gRPC transforms, gzip/deflate/Brotli, WebSocket `permessage-deflate` | managed IPv4/IPv6 origins and clients in the acceptance executable |
| HTTP/2 | WSK + kernel Schannel ALPN `h2` | concurrent streams, flow control, GOAWAY, HTTP/gRPC transforms, compression, Extended CONNECT WebSocket, unsupported CONNECT fail-close | managed IPv4/IPv6 H2 exchanges and evidence gate |
| HTTP/3 | inbox kernel MsQuic + TLS 1.3 over app-scoped bidirectional UDP tuple translation | SETTINGS, dynamic QPACK, multiplexing, stream-local block/reset, gRPC, compression, strict H3 origin and proof-gated H2/H1 fallback | managed QUIC exchanges, outbound DATAGRAM_DATA and reverse OUTBOUND_IPPACKET/network-send telemetry, no-replay and origin-security negative cases |
| WebTransport | kernel HTTP/3 service | Extended CONNECT, streams, datagrams, reset, and Capsules | managed QUIC acceptance |

Origin security covers native chain validation, exact leaf pinning, client
certificate authentication, negotiated ALPN, rollback after a failed security
replacement, and fail-closed unknown-CA, wrong-pin, and wrong-client cases.
Unsafe requests and every security failure are not replayed during fallback.
Resource tests cover stream, connection, pending-work, request-buffer, and
origin-allocation limits plus cancellation and clean drain.

HTTP/1 and HTTP/2 large wire/frame storage is leased from fixed nonpaged
workspace pools with explicit active-session quotas. Variable semantic
headers, bodies, and codec state use bounded crtsys nonpaged allocations; the
example does not describe those allocations as one arena. A lease shortage,
allocation failure, or protocol bound failure closes the affected flow, and a
clean stop/drain must return the active lease count to zero.

Before opening either listener, the driver also proves the pool's owning
contract: quota exhaustion fails closed, duplicate close is idempotent, the
pool facade may die before its lease, and a last lease released at
`DISPATCH_LEVEL` is destroyed by the runtime-owned PASSIVE cleanup domain. The
service ABI exposes this result and the controller and acceptance executable
refuse to continue unless it passed.

The WebSocket full-duplex relay uses `kernel::join_bidirectional`. Neither
direction can be synchronously waited on by itself: the first non-success
result cancels both transports, while a normal WebSocket close returns success
after exchanging the close frame and then both TLS `close_notify` messages. The parent
continues only after both relay coroutines have finished. This keeps an
async-stream resume worker from being blocked by the flow it must later resume.

Capture export has a stricter privacy boundary than in-kernel inspection.
Request header values, cookies, credentials, paths, and request bodies are not
exported. Only bounded structural metadata is logged. A decoded response is
written as `response.html` only when the kernel explicitly classified its
semantic content type as HTML.

## Build and contract tests

```powershell
cmake -S examples\wfp\kernel\browser-https-inspection `
      -B artifacts\examples\wfp-kernel-browser-https-inspection -A x64 `
      -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-kernel-browser-https-inspection `
      --config Release
ctest --test-dir artifacts\examples\wfp-kernel-browser-https-inspection `
      -C Release --output-on-failure
```

Host CTests validate parser, ABI, privacy, and evidence contracts. They do not
claim to execute WFP, Schannel, or kernel MsQuic.

## Observe an already-running browser

Run in an elevated test VM after installing and starting the driver:

```powershell
.\crtsys_wfp_kernel_browser_https_inspection_controller.exe `
    "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" `
    .\capture 30
```

The first argument is the exact executable path of a browser the user already
started. The controller changes no browser setting, policy, profile, proxy,
certificate-error switch, or protocol flag. It stops when the duration
expires, Ctrl+C is pressed, or `capture\stop.request` is created. Its WFP
session and temporary interception identities are removed on exit.

Ordinary Chromium mode blocks application-scoped UDP/443 so the unchanged
browser can use the inspected TCP fallback; it does not silently label that fallback as HTTP/3.
The separate acceptance executable proves the real kernel HTTP/3
path without changing browser behavior. Certificate pinning and a client's
private-CA restrictions remain client trust-policy boundaries; the example
does not claim to bypass certificate pinning.

## Managed runtime acceptance

```powershell
.\crtsys_wfp_kernel_browser_https_inspection_acceptance.exe .\capture
```

The optional argument is the evidence directory. The acceptance executable
locates and launches
`crtsys_wfp_kernel_browser_https_inspection_controller.exe` beside itself.
Do not launch the controller separately for this command. The controller owns
WFP and driver configuration; acceptance owns only controlled traffic and
verification. A PASS is emitted only after evidence is flushed and the
controller has acknowledged stop and exited successfully.

## Source layout

- `driver/`: kernel TLS/HTTP/QUIC processing and capture.
- `app/controller.cpp`: continuous already-running-browser controller.
- `app/control_server.cpp`, `app/managed_policy.cpp`: typed IPC command server
  and controller-owned policy used by runtime acceptance; no traffic
  generation or PASS logic.
- `test/wfp/runtime/fixtures/kernel/browser-https-inspection/`: controlled
  origins/clients, protocol scenarios, evidence gates, and acceptance main.
- `test/`: pure host contract tests.

Arbitrary informational responses, trailer rewriting, ECH key distribution,
and application-specific pinning exceptions require product policy beyond
this bounded example.
