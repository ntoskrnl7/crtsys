# NTL WFP samples

[WFP guide](../../docs/ntl/wfp-guide.md) ·
[한국어 설명](./README.ko-KR.md)

Samples are grouped by the runtime that owns application-content inspection
and transformation. `user` samples keep protocol policy in the controller or
proxy process. `kernel` samples execute their defining decision or rewrite in
the driver; their applications only configure policy and report telemetry.
Neutral reusable protocol code belongs in `<ntl/net/...>`, not in a sample
`common` directory.

Application-content samples are paired so the same goal can be compared across
the two runtimes.

| Goal | User-mode policy | Direct kernel policy | Shared semantic acceptance scope |
|---|---|---|---|
| TCP message permit/block | [`user/tcp-content-filter`](./user/tcp-content-filter/) | [`kernel/tcp-content-filter`](./kernel/tcp-content-filter/) | IPv4/IPv6, the same bounded sample-record framing, structured permit/block/malformed policy, and restoration after policy removal |
| UDP datagram permit/block | [`user/udp-content-filter`](./user/udp-content-filter/) | [`kernel/udp-content-filter`](./kernel/udp-content-filter/) | IPv4/IPv6 complete datagrams, the same structured record, permit/block/malformed policy, and restoration; only the user verdict path needs clone/reinject and an asynchronous queue |
| TCP endpoint | [`user/connect-redirect`](./user/connect-redirect/) | [`kernel/connect-redirect`](./kernel/connect-redirect/) | IPv4/IPv6 original tuple and opaque redirect records, loop-free original connect, bounded bidirectional coroutine relay, unavailable-peer fail-closed behavior without origin bypass, and restoration |
| TLS/HTTP content | [`user/tls-inspection-proxy`](./user/tls-inspection-proxy/) | [`kernel/tls-inspection-proxy`](./kernel/tls-inspection-proxy/) | Two validated Schannel legs, SNI identity selection, ALPN HTTP/1.1 and HTTP/2, bounded framing/HPACK, request/response transformation, permit/block, and IPv4/IPv6 original destination |
| Browser HTML | [`user/browser-https-inspection`](./user/browser-https-inspection/) | [`kernel/browser-https-inspection`](./kernel/browser-https-inspection/) | HTTP/1.1, HTTP/2, managed HTTP/3, bounded codecs and semantic content policy; the existing-browser mode never launches or reconfigures a browser |
| HTTP/3/QPACK | [`user/http3-inspection`](./user/http3-inspection/) | [`kernel/http3-inspection`](./kernel/http3-inspection/) | Real QUIC/TLS 1.3, bounded dynamic QPACK resume/ack, gzip/deflate/Brotli, Extended CONNECT, WebTransport streams/Datagrams/capsules/reliable reset, and exact shutdown |

The paired examples do not maintain look-alike policy copies. Each pair consumes
one policy or record contract from [`shared`](./shared/): only the transport,
scheduler, and execution address space differ. The repository parity contract
fails if either side stops consuming that shared policy.

`ale-connect-block`, `async-inspection`, `bind-redirect`, `datagram-proxy`,
`flow-monitor`, `stream-edit`, and `specialized-observation` teach WFP callout
primitives and driver lifetime, so they remain kernel-only instead of gaining
empty user copies. The user side of the table demonstrates policy-service
offload; the kernel side demonstrates direct processing with the same semantic
and bounded limits.

Those seven are kernel-only for concrete reasons: `ale-connect-block` is the
minimal classify/action lesson; `async-inspection` owns pended operations and
retained packets; `bind-redirect` mutates writable bind requests;
`datagram-proxy` owns NBL clone/reinjection; `flow-monitor` owns WFP flow
contexts; `stream-edit` owns stream continuation/injection; and
`specialized-observation` registers IPsec, MAC/frame, vSwitch, endpoint-close,
and fast-layer capabilities. A user directory for any of them would only be a
controller for the same kernel mechanism, not an alternate user-mode data
plane.

Start with [`ale-connect-block`](./kernel/ale-connect-block) or its
[Korean walkthrough](./kernel/ale-connect-block/README.ko-KR.md). Its name describes
the observable behavior: a kernel callout blocks one selected outbound IPv4
TCP connection, then ephemeral policy removal restores the connection.

Use [`connect-redirect`](./user/connect-redirect) when a selected TCP connection
must traverse a local user-mode proxy. It preserves WFP redirect records,
automatically reconnects to the original destination without a loop, and
relays both directions with IOCP coroutines.

Use [`kernel/connect-redirect`](./kernel/connect-redirect/) when the proxy data
plane must stay in the driver. `<ntl/net/kernel/wsk_redirect>` captures the
accepted WSK socket's original tuple and opaque redirect records, attaches the
records before the outbound WSK connect, and relays both directions with
bounded kernel coroutines over IPv4 and IPv6.

Use [`tls-inspection-proxy`](./user/tls-inspection-proxy) when that proxy must
terminate TLS in user mode before applying an application-content policy. It
uses one Schannel session on each side, selects a bounded per-SNI identity,
frames bounded HTTP/1.1 and ALPN `h2` plaintext, proves HTTP/2
request/response transformation, keeps CA/trust policy injectable, and leaves
TLS keys and plaintext out of the kernel callout.

Use [`browser-https-inspection`](./user/browser-https-inspection) for a long-running
browser workflow. It scopes redirect policy to one browser path, logs bounded
decoded HTML responses over HTTP/1.1 and HTTP/2, inspects negotiated
`permessage-deflate` WebSocket messages, and provides a runtime wrapper for
continuous observation of an already-running browser selected by its exact
executable path. The wrapper never creates a profile, launches or terminates
the browser, or adds browser command-line flags.

Use [`kernel/tls-inspection-proxy`](./kernel/tls-inspection-proxy/) and
[`kernel/browser-https-inspection`](./kernel/browser-https-inspection/) to run
the corresponding controlled HTTP/1.1 and HTTP/2 decisions over driver-owned
WSK and Schannel and HTTP/3 over kernel MsQuic. The separate browser
acceptance covers H1/H2/H3 permit and block in one run. Its example controller
owns policy and driver control, while the adjacent acceptance executable and
all managed clients/origins live under `test/wfp/runtime/fixtures`.
Continuous browser mode observes an already-running exact executable path and
changes no browser setting or command-line feature flag.

Use [`http3-inspection`](./user/http3-inspection) to implement the boundary above a
QUIC terminator. Its deterministic backend delivers arbitrarily split
decrypted streams; NTL reassembles HTTP/3 frames, exercises bounded dynamic
QPACK blocked-stream resume and acknowledgements, decodes
gzip/deflate/Brotli, and validates Extended CONNECT plus WebTransport streams,
Datagrams, capsules, and reliable reset. A product QUIC backend remains
responsible for TLS 1.3, packet recovery, and stream lifetime. This is an
application-managed endpoint contract, not a promise that an unchanged
Chromium browser will accept a private-CA identity for arbitrary origins.

Use [`kernel/http3-inspection`](./kernel/http3-inspection/) for an actual kernel
MsQuic NMR provider with the same dynamic QPACK, codecs, Extended CONNECT and
WebTransport semantics, plus content-based 200/403 and connection-lifetime
evidence.

For user-mode content decisions, choose the transport-specific example:

- [`udp-content-filter`](./user/udp-content-filter) receives one complete outbound
  datagram, then either reinjects its retained clone or discards it; and
- [`tcp-content-filter`](./user/tcp-content-filter) frames complete inbound
  application messages from a byte stream, then continues the deferred frame
  or drops the whole flow.

Both policy coroutines receive bounded owning copies and can only return typed
verdicts. The TCP sample's four-byte big-endian prefix is explicitly a sample
application protocol, not a TCP header.

The public surface is intentionally split:

- `<ntl/wfp/callout>` and `<ntl/wfp/classify>` are kernel-only;
- `<ntl/wfp/management>` is user-only; and
- `<ntl/wfp/all>` selects the appropriate side.

Packet and stream samples share `<ntl/net/buffer/scatter_view>` for allocation-free
fragment traversal, while `<ntl/net/buffer/owned_bytes>` makes callback-lifetime copies
explicit. `<ntl/net/io/async_byte_stream>` and `<ntl/wfp/stream_reader>` add bounded
single-reader coroutine observation; they do not turn already-permitted WFP
bytes into a deferred enforcement path.

See [`docs/ntl/wfp.md`](../../docs/ntl/wfp.md) for the ownership rules and the
Windows-driver-samples coverage map.

For Visual Studio/NuGet, select **NTL WFP** on the crtsys WDM entry-point
property page. It supplies the entry point, `fwpkclnt.lib`, and the packaged
kernel zlib/Brotli backend. CMake projects use `WFP NTL`; direct kernel content
codecs and pinned MsQuic headers remain explicit optional targets as described
in the WFP documentation. No build step installs a driver or MsQuic provider.

## Visual Studio solutions

Every directory under `user` and `kernel` now contains a checked-in
`*_vs.sln`. Open that solution to build the example's WDM callout driver and
its controller or policy service together. The browser user-mode example also
contains its separate HTTP/3 proxy-service project. All driver projects select
`NtlWfp`; the HTTP/3 application projects restore the pinned native MsQuic
package and copy `msquic.dll` beside the executable.

For example:

```powershell
msbuild .\examples\wfp\kernel\ale-connect-block\crtsys_wfp_ale_connect_block_vs.sln `
  /restore /p:Configuration=Debug /p:Platform=x64
```

The solutions intentionally contain the example-facing driver and
controller/service targets. Acceptance traffic generators and focused contract
executables remain under `test/wfp` and are built by CMake, so the example
solution is not obscured by test-only projects. The `.vcxproj` and solution
files are generated from one reviewed manifest; validate them with:

```powershell
.\scripts\examples\Generate-WfpVisualStudioProjects.ps1 -Check
.\scripts\ci\Test-CrtSysWfpExampleProjects.ps1
```

Run these commands from the repository root. Edit the generator manifest when
a public CMake target or source list changes instead of hand-editing a
generated project. Building a solution only compiles and packages its output;
it never installs or loads the driver on the host.
