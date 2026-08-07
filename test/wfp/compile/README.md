# ntl::wfp compile contracts

This fixture compiles the public WFP ownership and policy contracts with
`/W4 /WX`.

The kernel target covers:

- the public `<ntl/net/kernel/all>` aggregate dual-runtime header, allocation-free
  gRPC/WebSocket/datagram framing, fragmented static/Huffman QPACK, bounded
  ClientHello observation, direct/offloaded transform metadata, typed
  inspection verdicts, synchronous/asynchronous offload seams, the QUIC
  provider seam, and drainable kernel lifetimes;
- strongly typed layer/callout keys and non-escaping callback events;
- ALE, flow-established, packet, and stream callback signatures;
- flow-context ownership transfer, flow-delete destruction, and unload
  draining before callout unregistration;
- ALE pending-operation ownership;
- IPv4 and IPv6 ALE connect-redirection mutation, loop detection, and
  writable-data finalization;
- cloned-packet network and transport injection;
- callback-scoped packet/stream scatter views and bounded owned copies;
- the single-reader coroutine stream and WFP observation adapter; and
- reverse-order callout teardown.

The user target compiles a provider/sublayer/callout/filter graph containing
terminating ALE policy, non-terminating flow inspection, and terminating stream
editing plus unknown-action stream control. Layer-scoped condition builders
cover typed IPv4/IPv6 networks, ports, protocols, identities, flags, VLANs,
MAC addresses, and atomic ICMP type/code values; duplicate or unsupported
fields are rejected before an engine transaction exists. It also compiles the
mandatory PID/port connect-redirect builder, persistent manifest
reconciliation, bounded network-event telemetry, and the move-only user proxy
handoff. Native engine handles and action/condition arrays remain
inaccessible.

The portable semantic target executes fragmented big-endian reads, subviews,
copies, cross-fragment writes, early-stop enumeration, and token matching on
both x64 and x86.

The user-mode async-socket target compiles the IOCP context, move-only socket,
and `co_await` read/write operations with `/W4 /WX`. The stream-edit
controller's `--coroutine-self-test` is the corresponding loopback runtime
contract.

The user-mode TLS target executes real loopback Schannel client/server
sessions on x64 and x86. It forces handshake fragmentation, transfers
multi-record request and response payloads, parses the original ClientHello,
dynamically issues and selects an SNI leaf, verifies it through an
application-owned private CA chain, rejects a custom policy, exercises
bounded TLS framing plus HTTP/1 `Content-Length`/`chunked` contracts, consumes
TLS 1.3 post-handshake messages, and proves `close_notify` in both directions.

The portable protocol target executes WebSocket fragmentation and negotiated
`permessage-deflate` context takeover, HTTP/2 frame and continuation assembly,
RFC 7541 Huffman and dynamic-table HPACK vectors, split HTTP/3 streams and
RFC 9204 static QPACK vectors, gzip/deflate/Brotli chains and their corruption
and expansion limits, ALPN selection, and fail-closed TLS policy contracts on
x64 and x86.

The same portable target cross-checks the allocation-free kernel-facing core
in user mode. It rejects malformed offload metadata, verdicts, and ClientHello
compression methods; exercises inline async completion, cancel, stop, and
drain; and verifies that protocol, direction, flow, and port metadata survive
the explicit offload boundary.

The HTTP transform target runs one semantic request/response policy across
HTTP/1.1 and HTTP/2 adapters. It covers chunked transfer decoding, stateless
HPACK output, fragmented HEADERS/CONTINUATION, multiplexed request/response
correlation, DATA reconstruction, nested gzip/Brotli re-encoding, validator
removal, fail-closed and restore-original failures, blocking, and synthetic
responses. It also rejects control-character injection and forbidden
trailers, and verifies HEAD/304 bodyless framing without misreading
representation `Content-Length` or `Content-Encoding`. The browser HTTP/3
proxy contract applies the same pipeline to a gzip response, verifies the
re-encoded result, and checks the same HEAD metadata rule.

The HTTP/2 contracts also exercise the bounded kernel preflight boundary.
They verify that a first request can be blocked before an origin connection,
that a permitted request transfers its move-only workspace and buffered
frames exactly once, and that a browser-facing local SETTINGS acknowledgement
is consumed locally instead of being replayed to the later origin connection.

The transform contracts also execute the fixed-pool coroutine policy path,
including cancellation, deadline, and queue overload. The streaming contract
runs the same chunk policy with correct HTTP/1.1, HTTP/2, and HTTP/3 header
framing. The policy stress target completes 4,096 queued asynchronous messages
and a 64 MiB bounded streaming body.

The protocol target additionally rewrites masked WebSocket messages, gzip
gRPC messages split across arbitrary DATA chunks, and WebTransport stream and
capsule payloads. It validates product actions for unavailable QUIC, ECH,
pinning, and successful HTTP/2 interception.

The browser HTTP/3 target directly compiles the browser example's
`http3_inspection.*` and executes split request/response streams, a separate
QUIC FIN, static QPACK, Brotli HTML decoding, and HTML logging on x64 and x86.

CTest registers the executable semantic contracts. The management
contract checks every IPv4 prefix from 0 through 32 and every IPv6 prefix from
0 through 128 with deterministic generated addresses. The inspection contract
feeds 8,192 deterministic fragmented inputs through framing and content-search
invariants. The deterministic parser fuzz target adds 16,384 generated inputs
plus seed truncation and mutation across HTTP/1, HTTP/2+HPACK, HTTP/3+QPACK,
WebSocket, gRPC, capsules, gzip/deflate/Brotli, and TLS ClientHello. Debug and
Release both run these contracts on x64 and x86.

`Run-WfpLibFuzzer.ps1` is the optional coverage-guided gate. It configures the
same bounded parser harness with ClangCL, libFuzzer, and AddressSanitizer, uses
the static MSVC runtime required by libFuzzer on Windows, seeds valid HTTP/TLS
inputs, supplies `wfp-fuzz.dict`, and preserves every crashing artifact under
the selected build directory. The deterministic executable remains the normal
MSVC CTest contract; the libFuzzer target is opt-in so ordinary WDK builds do
not acquire a Clang dependency.

```powershell
.\Run-WfpLibFuzzer.ps1 -Seconds 1800
```
