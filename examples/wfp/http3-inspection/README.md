# HTTP/3 inspection backend contract

[한국어 설명](./README.ko-KR.md)

This deterministic user-mode example demonstrates the boundary between a
QUIC implementation and NTL protocol inspection. UDP datagrams are not
treated as complete HTTP messages. A backend reports negotiated `h3`,
decrypted stream bytes with stable stream IDs, arbitrary callback splits,
stream FIN/reset, encoder-stream bytes, and connection shutdown.

NTL reassembles bounded HTTP/3 frames and decodes QPACK. The reusable dynamic
decoder implements bounded RFC 9204 encoder instructions, dynamic entries,
required insert counts, blocked-stream resume, decoder acknowledgements, and
stream cancellation. The example also verifies a decoded Brotli HTML body.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
.\build\Debug\crtsys_wfp_http3_inspection.exe
```

`<ntl/net/http3/backend>` is the transport-provider contract.
`<ntl/net/http3/qpack>` is the bounded QPACK implementation.
`<ntl/net/http/datagram>` implements RFC 9297 HTTP Datagrams and Capsule Protocol
framing. `<ntl/net/http/extended_connect>` validates HTTP/2 and HTTP/3 extended
CONNECT. `<ntl/net/http3/webtransport>` contains bounded draft-16 WebTransport
session, capsule, and stream-prefix state.

Capabilities are explicit. A backend that provides only decoded ordinary
request/response callbacks must report raw streams, HTTP Datagrams, extended
CONNECT, and WebTransport as unavailable. The pinned msh3 adapter used by the
browser sample has exactly that limitation. A full WebTransport runtime needs
a QUIC backend exposing bidirectional/unidirectional stream and Datagram
primitives, not a parser change or an unsafe capability claim.
