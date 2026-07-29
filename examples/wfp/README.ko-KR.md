# NTL WFP 예제

[English](./README.md)

처음에는 [`ale-connect-block`](./ale-connect-block/README.ko-KR.md)부터
보십시오. 커널 callout이 선택한 outbound IPv4 TCP 연결 하나를 차단하고,
동적 정책을 제거하면 같은 연결이 다시 허용되는 가장 작은 예제입니다.

선택한 TCP 연결을 사용자 모드 proxy로 보내려면
[`connect-redirect`](./connect-redirect/README.ko-KR.md)를 사용합니다. 원래
목적지와 WFP redirect record를 보존하고 IOCP coroutine으로 양방향 byte
stream을 중계합니다.

TLS 평문 내용까지 검사하려면
[`tls-inspection-proxy`](./tls-inspection-proxy/README.ko-KR.md)를 사용합니다.
TLS와 인증서 정책은 사용자 모드 Schannel 계층에 있고, 커널 callout은 연결을
검사 경로로 강제할 뿐 평문이나 TLS key를 받지 않습니다.

브라우저 HTTPS HTML을 계속 기록하려면
[`browser-https-inspection`](./browser-https-inspection/README.ko-KR.md)을
사용합니다. 선택한 브라우저 실행 경로만 redirect하고 HTTP/1.1·HTTP/2 HTML,
gzip/deflate/Brotli와 협상된 WebSocket `permessage-deflate`를 bounded하게
검사합니다. 런타임 스크립트는 격리 profile과 임시 테스트 CA 신뢰를
관리하지만 브라우저 feature flag나 policy를 변경하지 않습니다.

QUIC terminator 위의 HTTP/3 검사 경계는
[`http3-inspection`](./http3-inspection/README.ko-KR.md)을 참고하십시오.
결정적 backend가 임의로 나뉜 복호화 stream을 전달하면 NTL이 HTTP/3 frame,
bounded static QPACK과 Brotli HTML을 검사합니다. TLS 1.3, 패킷 복구, stream
수명과 동적 QPACK table은 제품 QUIC provider의 책임입니다. 이는 앱이 관리하는
endpoint 계약이며, 변경하지 않은 Chromium 브라우저가 임의 origin에 대한
사설 CA identity를 받아들인다는 뜻은 아닙니다.

사용자 모드에서 내용으로 허용·차단하는 예제는 전송 방식별로 나뉩니다.

- [`udp-content-filter`](./udp-content-filter/README.ko-KR.md)는 완전한 UDP
  datagram 하나를 판단하여 보관한 clone을 재주입하거나 폐기합니다.
- [`tcp-content-filter`](./tcp-content-filter/README.ko-KR.md)는 byte stream에서
  명시한 application framing으로 완전한 메시지를 만든 뒤 지연한 frame을
  계속 진행하거나 flow 전체를 차단합니다.

TCP 예제의 4바이트 big-endian 길이는 TCP 표준 header가 아니라 예제
application protocol의 규칙입니다.

공개 API는 실행 계층별로 분리합니다.

- `<ntl/wfp/callout>`과 `<ntl/wfp/classify>`는 커널 전용입니다.
- `<ntl/wfp/management>`는 사용자 모드 전용입니다.
- `<ntl/wfp/all>`은 환경에 맞는 API를 선택합니다.

패킷과 stream 예제는 `<ntl/net/buffer/scatter_view>`로 조각난 MDL을 할당 없이 순회하고,
callback 이후에도 필요한 데이터만 `<ntl/net/buffer/owned_bytes>`로 명시적으로
복사합니다. `<ntl/net/io/async_byte_stream>`과 `<ntl/wfp/stream_reader>`는 bounded
single-reader coroutine 관찰을 제공하지만 이미 permit한 WFP byte를 나중에
차단하는 기능은 아닙니다.

소유권 규칙과 Windows driver sample 대응표는
[`docs/ntl/wfp.md`](../../docs/ntl/wfp.md)를 참고하십시오.
