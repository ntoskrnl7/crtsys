# NTL WFP samples

[한국어 설명](./README.ko-KR.md)

Start with [`ale-connect-block`](./ale-connect-block) or its
[Korean walkthrough](./ale-connect-block/README.ko-KR.md). Its name describes
the observable behavior: a kernel callout blocks one selected outbound IPv4
TCP connection, then ephemeral policy removal restores the connection.

Use [`connect-redirect`](./connect-redirect) when a selected TCP connection
must traverse a local user-mode proxy. It preserves WFP redirect records,
automatically reconnects to the original destination without a loop, and
relays both directions with IOCP coroutines.

Use [`tls-inspection-proxy`](./tls-inspection-proxy) when that proxy must
terminate TLS in user mode before applying an application-content policy. It
uses one Schannel session on each side, selects a bounded per-SNI identity,
frames HTTP/1.1 plaintext, keeps CA/trust policy injectable, and leaves TLS
keys and plaintext out of the kernel callout.

Use [`browser-https-inspection`](./browser-https-inspection) for a long-running
browser workflow. It scopes redirect policy to one browser path, logs bounded
decoded HTML responses over HTTP/1.1 and HTTP/2, inspects negotiated
`permessage-deflate` WebSocket messages, and provides a runtime wrapper for
isolated browser launch and temporary test-CA trust.

Use [`http3-inspection`](./http3-inspection) to implement the boundary above a
QUIC terminator. Its deterministic backend delivers arbitrarily split
decrypted streams; NTL reassembles HTTP/3 frames, decodes the bounded static
QPACK profile, and applies the same Brotli content decoder used by the browser
example. A product QUIC backend remains responsible for TLS 1.3, packet
recovery, stream lifetime, and any negotiated dynamic QPACK table. This is an
application-managed endpoint contract, not a promise that an unchanged
Chromium browser will accept a private-CA identity for arbitrary origins.

For user-mode content decisions, choose the transport-specific example:

- [`udp-content-filter`](./udp-content-filter) receives one complete outbound
  datagram, then either reinjects its retained clone or discards it; and
- [`tcp-content-filter`](./tcp-content-filter) frames complete inbound
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
