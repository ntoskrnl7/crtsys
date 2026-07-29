# HTTP/3 검사 backend 계약

[English](./README.md)

이 결정적 user-mode 예제는 QUIC 구현과 NTL protocol 검사 사이의 경계를
보여 줍니다. UDP datagram 하나를 완전한 HTTP message로 가정하지 않습니다.
backend는 협상된 `h3`, 안정적인 stream ID와 함께 복호화된 stream byte,
임의의 callback 분할, stream FIN/reset, QPACK encoder stream, connection
종료를 보고해야 합니다.

NTL은 제한된 크기 안에서 HTTP/3 frame을 재조립하고 QPACK을 해석합니다.
범용 동적 decoder는 RFC 9204 encoder instruction, dynamic entry, required
insert count, blocked stream 재개, decoder acknowledgement, stream cancel을
구현합니다. 예제는 Brotli로 압축된 HTML도 byte 단위로 검증합니다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
.\build\Debug\crtsys_wfp_http3_inspection.exe
```

`<ntl/net/http3/backend>`는 transport provider 계약이고
`<ntl/net/http3/qpack>`은 bounded QPACK 구현입니다.
`<ntl/net/http/datagram>`은 RFC 9297 HTTP Datagram과 Capsule Protocol framing,
`<ntl/net/http/extended_connect>`는 HTTP/2·HTTP/3 extended CONNECT 검증,
`<ntl/net/http3/webtransport>`는 WebTransport-over-HTTP/3 draft-16의 bounded
session, capsule, stream-prefix 상태를 제공합니다.

capability는 명시적으로 보고합니다. 일반 request/response callback만
제공하는 backend는 raw stream, HTTP Datagram, extended CONNECT,
WebTransport를 지원한다고 표시하면 안 됩니다. 브라우저 예제의 pinned msh3
adapter가 이 경우입니다. 실제 WebTransport runtime에는 parser 변경이 아니라
양방향·단방향 QUIC stream과 Datagram primitive를 노출하는 backend가
필요합니다.
