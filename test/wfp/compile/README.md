# ntl::wfp compile contracts

This fixture compiles the public WFP ownership and policy contracts with
`/W4 /WX`.

The kernel target covers:

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
editing plus unknown-action stream control. It also compiles the mandatory
PID/port connect-redirect builder and the move-only user proxy handoff. The
dynamic engine handle and native action/condition arrays remain inaccessible.

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

The browser HTTP/3 target directly compiles the browser example's
`http3_inspection.*` and executes split request/response streams, a separate
QUIC FIN, static QPACK, Brotli HTML decoding, and HTML logging on x64 and x86.
