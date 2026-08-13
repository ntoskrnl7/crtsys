# WFP 브라우저 HTTPS 검사

[English](./README.md)

이 예제는 NTL의 WFP, TLS, HTTP/1.1, HTTP/2, HTTP/3, WebSocket, 콘텐츠
디코딩 기능을 브라우저 검사기로 조립한 범용 예제입니다. 특정 PC 경로, 공개
사이트, 인증서 또는 브라우저 프로필을 드라이버나 앱에 고정하지 않습니다.

동적 WFP 정책이 활성화된 동안에는 지정한 브라우저 실행 파일만 대상으로
다음 규칙을 적용합니다.

- IPv4/IPv6 TCP 443을 로컬 Schannel 검사 프록시로 리디렉션합니다.
- IPv4/IPv6 UDP 443을 차단하여 설정을 바꾸지 않은 Chromium 브라우저가
  검사 가능한 TCP 경로로 fallback하게 합니다.

wrapper는 해당 실행 파일 경로에서 이미 실행 중인 브라우저를 관찰합니다.
브라우저를 실행하거나 종료하지 않고, 프로필을 만들거나 삭제하지 않으며,
프로필·기능·인증서·QUIC·ECH·호스트 매핑·로깅 인자를 전달하지 않습니다.
예제 inspection CA만 VM의 신뢰 저장소에 임시 등록하고 종료할 때 제거합니다.

TCP 경로는 WFP가 보존한 원래 목적지를 복구하고, 제한된 크기의 ClientHello에서
SNI를 읽어 짧은 수명의 호스트별 leaf를 발급합니다. 브라우저 TLS를 종료한 뒤
정상 인증서 검증을 사용하는 origin TLS 연결을 만들고 HTTP/1.1, HTTP/2,
HTTP/1.1 Upgrade WebSocket과 HTTP/2 Extended CONNECT WebSocket을 검사합니다.

HTTP/1.1, HTTP/2, raw MsQuic HTTP/3 서비스는 같은 의미 계층
`ntl::net::http::inspection_policy`를 사용합니다. 변환 정책은 조립·디코딩이
끝난 메시지의 헤더와 본문을 검사·수정하고, 결정 정책은 method, scheme,
authority, path, query, 헤더·trailer·본문뿐 아니라 연결 endpoint, 프로세스와
애플리케이션 identity, WFP flow, SNI, ALPN을 함께 보고 permit, block,
drop-flow를 선택할 수 있습니다. 응답 검사 중에는 연결된 원래 요청도 같은
context에서 조회합니다. 프로토콜 adapter가 chunking, HPACK/QPACK, 압축
재인코딩, Content-Length, multiplexing, flow control을 담당합니다.

공유 정책은 정확한 `application/grpc` media-type 계열도 식별하고 HTTP/1.1,
HTTP/2, HTTP/3의 완성된 gRPC 메시지에 제한된 크기의
`ntl::net::grpc::message_transform_pipeline`을 적용합니다. 잘린 gRPC
envelope는 fail closed하며
`application/grpcfoo` 같은 유사 형식은 일반 HTTP로 취급합니다. tunnel 정책도
프로토콜별입니다. H2 WebSocket과 H3 WebTransport만 허용하고 H2 WebTransport,
H3 WebSocket, 일반 CONNECT는 fail closed합니다.

## 관리형 HTTP/3 경로

HTTP/3 프록시 서비스는 브라우저 controller와 명확히 분리되어 있습니다.
이 서비스가 QUIC endpoint를 소유하고 WFP 모드에서는 앱 범위 UDP/443 정책도
소유합니다. 클라이언트는 원래 SNI와 `:authority`를 유지하며 private CA 선택은
WFP 드라이버가 아니라 클라이언트 통합 계층의 책임입니다.

선택적인 WFP UDP/443 관리형 tuple translation은 FLOW_ESTABLISHED에서 원래
tuple을 기록하고 DATAGRAM_DATA에서 outbound 클라이언트 데이터그램을
리디렉션합니다. 로컬 프록시 응답은 OUTBOUND_IPPACKET에서 검증하고 크기가
제한된 새 NBL로 복사하여 원래 source tuple로 복원한 뒤 network-send로
재주입합니다. connect-redirect의 원래 목적지 컨텍스트는 만들지 않습니다.
관리형 프로토콜이 원래 SNI와 `:authority`를 이미 소유하기 때문입니다. 반대로
일반 브라우저 TCP 리디렉션은 accept된 소켓에서 원래 목적지를 복구해야 하므로
컨텍스트를 계속 사용합니다.

관리형 프록시는 origin HTTP/3를 먼저 시도합니다. 외부 QUIC이 transport,
연결 또는 timeout 오류로 실패한 경우에만 정상 인증서 검증을 사용하는
TLS/TCP로 재시도할 수 있습니다. 실제 upstream은 요청마다 `h3`, `h2` 또는
`http/1.1`로 기록합니다. 인증서, mTLS, 요청 검증 실패는 fallback 사유가
아니며 그대로 실패합니다.

`events.log`에는 호스트, 실제 협상 프로토콜, 상태 코드, 콘텐츠 형식, 인코딩,
복원된 본문 크기를 기록합니다. `.html`은 서버 응답 본문이며 JavaScript 실행
뒤의 DOM snapshot은 아닙니다. 요청 경로·query·헤더·쿠키·인증 정보·본문은
metadata 로그에 기록하지 않습니다. 디코딩된 HTML 캡처만 Content-Type으로
명시적으로 확인한 뒤 저장합니다.

## 소스 구성

- `main.cpp`: 실제 브라우저 controller 명령행 해석
- `browser_runtime.*`: listener, WFP 수명, 공유 IOCP 연결 task registry,
  인증서, task drain 종료
- `browser_policy.*`: 앱 범위 TCP/UDP WFP 필터
- `browser_proxy.*`: TCP ClientHello, Schannel, ALPN 프로토콜 분기
- `http1_inspection.*`: 제한된 HTTP/1.1 및 WebSocket relay
- `http1_inspection_support.*`: 직접 테스트할 수 있는 요청 재작성, 콘텐츠
  복원, WebSocket 확장 협상
- `http2_inspection.*`: 범용 NTL 연결·세션 adapter에 예제 전용 HTTP/2 로깅과
  WebSocket 정책을 연결
- `http3_inspection.*`: 브라우저 전용 HTTP/3 정책과 개인정보를 남기지 않는
  검사 observer
- `http3_live_proxy.*`: 범용 MsQuic server, HTTP/3 connection, 비동기 origin
  pool에 동적 TLS identity와 listener를 연결하는 조립 코드
- `http3_origin.*`: strict-H3 및 transport-fallback origin 정책
- `http3_proxy_service.*`, `http3_service_main.cpp`: 별도 H3 프록시 서비스,
  WFP 정책 소유권, ready/stop 수명 관리
- `bidirectional_relay.hpp`: 구조화된 비동기 relay 완료, 반대 방향 취소 및
  task 종료 대기
- `browser_log.*`: event 및 HTML 출력

범용 구현은 `include/ntl`의 `ntl/net/http/inspection_policy`,
`ntl/net/http/decision_policy`, `ntl/net/http/inspection_context_view`,
`ntl/net/http/inspection_conditions`,
`http1_proxy_connection`, `http2_proxy_connection`, `http2_proxy_session`,
`http2_websocket_tunnel`, `http3_backend`, `http3_msh3_client`,
`http3_msquic_backend`, `http3_msquic_runtime`, `http3_msquic_server`,
`http3_async_origin_pool`, `http3_proxy_connection`, `http3_inspection_proxy`,
`http3_standard_inspection_proxy`, `http3_qpack`, `http_datagram`,
`http_extended_connect`, `webtransport_http3`, `webtransport_session`,
`content_stream`, `tls_inspection_frontend`, `tls_product_backend`에
있습니다. HTTP/3 connection은 요청을 검증하고 SNI와 `:authority`를 결속하며
제한된 디코딩과 typed 요청/응답 정책을 적용합니다. control stream, SETTINGS,
QPACK, request stream 상태, Extended CONNECT, WebTransport, terminal response,
reset, 취소, drain 종료도 범용 계층이 담당합니다. origin adapter에는 검증을
마친 요청만 전달하며 예제에는 브라우저 정책, 개인정보 보호 로깅, 인증서 선택,
origin 선택만 남습니다.

허용·차단 결정은 본문만 보는 콜백이 아닙니다. 메서드, scheme,
authority, path/query, 요청·응답 헤더와 trailer, 디코딩된 본문,
HTTP 버전과 stream ID, PID·애플리케이션 ID·원래 목적지·WFP flow,
TLS SNI와 ALPN을 같은 `inspection_context_view`에서 조합할 수 있습니다.
미리 제공되는 조건은 닫힌 DSL이 아니라 편의 함수입니다. 예를 들어
사용자 정의 헤더 namespace는 다음처럼 검사합니다.

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

이름 접두사만 조건이면 `header_name_starts_with("custom-")` 하나면
충분합니다. 이름과 값의 관계를 직접 정의할 때는 `any_header(...)`,
HTTP·TLS·WFP 문맥을 함께 조합할 때는 raw `when(inspection_context_view)`를
사용합니다. 응답 정책에서 요청 헤더를 볼 때는
`request_header_name_starts_with`, 응답 헤더만 볼 때는
`response_header_name_starts_with`를 사용합니다. 헤더 이름은 NTL이
소문자로 정규화하므로 helper는 대소문자 차이까지 안전하게 처리합니다.

같은 이름의 헤더가 반복되면 `header_is` 계열은 모든 필드를 검사하므로
첫 번째 정상 값 뒤의 악성 값을 숨기는 방식으로 우회할 수 없습니다.

같은 방향과 단계의 규칙은 등록한 순서대로 평가하며, 처음 일치한 규칙이 최종
결정을 내립니다. 명시적 허용 목록을 만들 때는 조건이 좁은 허용 규칙을 조건 없는
차단 규칙보다 먼저 등록하세요. 일치하는 규칙이 없으면 해당 단계는 허용됩니다.

## 빌드와 실행

```powershell
cmake -S examples\wfp\user\browser-https-inspection `
      -B artifacts\examples\wfp-browser-https-inspection -A x64
cmake --build artifacts\examples\wfp-browser-https-inspection `
      --config Release
```

빌드 결과는 실제 브라우저 controller와 별도 HTTP/3 프록시 서비스입니다.

```text
crtsys_wfp_browser_https_inspection_controller.exe <browser.exe> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_http3_proxy_service.exe --managed-http3-proxy <listen-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_http3_proxy_service.exe --wfp-managed-http3-proxy <client.exe> <listen-port> <log-directory> [duration-seconds]
```

먼저 [실시간 runtime 가이드](../../../../test/wfp/runtime/https-live/README.ko-KR.md)에
설명된 대로 portable 패키지를 준비하세요. 해당 패키지의 관리자 PowerShell에서
브라우저를 평소 방식으로 열어 둡니다. 그다음 observer를 실행하고 이미 열린 창에서
검사할 페이지로 이동합니다.

```powershell
$inspectionUrl = [uri](Read-Host '검사할 HTTPS URL')
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Urls @($inspectionUrl) `
    -RequireQuicBlockedFallback `
    -LogDirectory (Join-Path (Get-Location) 'browser-log') `
    -DurationSeconds 90 `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

`-Urls`는 기대하는 캡처 목록이며 브라우저를 탐색시키는 인자가 아닙니다.
`-RequireQuicBlockedFallback`은 브라우저 설정 인자가 아니라 자동화
assertion입니다. 앱 범위 native IPv4/IPv6 UDP/443 차단 필터를 검증하고, 출력된
각 filter ID가 같은 실행의 bounded inventory에 있는지 대조하며, 그 필터와
일치하는 WFP `classify_drop` net event와 TCP로 검사된 새 HTML을 모두 요구합니다.
일치하는 UDP 시도가 없으면 차단 성공으로 오판하지 않고 검증 불충분으로
실패합니다. 로그 디렉터리에는 `wfp-policy-diagnostics.log`,
`browser-transport-evidence.json`, 프록시 로그와 캡처된 HTML이 남습니다. Edge
NetLog와 callout `action_write` 카운터는 증거로 사용하지 않습니다.

서비스는 endpoint와 선택적인 WFP 정책이 모두 활성화된 뒤에만 로그
디렉터리에 `service.ready`를 만듭니다. 같은 디렉터리에 `stop.request`를
만들면 연결을 drain한 뒤 종료합니다. 트래픽 생성기, private test origin,
자동 E2E 판정은 `test/wfp/runtime/fixtures/user/browser-https-inspection`에
있으며 두 제품 예제 실행 파일의 모드가 아닙니다.

## 지원 범위와 제한

TCP 경로는 Schannel이 협상하는 TLS 1.2/1.3, 지속 연결 HTTP/1.1 HTML,
multiplexing된 HTTP/2 HTML, HTTP/1.1 Upgrade WebSocket 및 RFC 8441 Extended
CONNECT WebSocket을 지원합니다. 두 WebSocket 경로는 완성된 RFC 6455
메시지와 협상된 `permessage-deflate`를 검사하고 변환합니다. HTTP/2 DATA
경계는 WebSocket frame 경계로 간주하지 않습니다. 제한된 stream framer가
여러 DATA frame에 나뉜 WebSocket frame을 이어 붙이고 변환 결과를 새 DATA
frame으로 인코딩합니다. 이 예제에서 그 밖의 Extended CONNECT protocol은
WebSocket으로 잘못 해석하지 않고 명시적인 fail-closed 정책으로 차단합니다.
범용 HTTP/2 proxy session도 fail-closed입니다. 일반 CONNECT와 Extended
CONNECT 모두 origin으로 HEADERS를 쓰기 전에 선택된 handler의 승인 메서드를
호출합니다. 기본값은 차단이며, 실제 byte-tunnel 계약을 구현한 handler가
passthrough를 명시적으로 선택한 경우에만 opaque DATA를 그대로 전달합니다.

HTTP/1 proxy도 CONNECT와 Upgrade를 origin에 쓰기 전에 같은 승인 절차를
적용합니다. 이 예제는 검증된 WebSocket Upgrade만 검사 모드로 승인하며, 일반
CONNECT와 알 수 없는 protocol switch는 origin에 전송하지 않고 제한된 로컬
403 응답으로 거부합니다.

HTTP/1 또는 HTTP/2 요청을 단일 TLS origin 연결로 쓰기 전에 adapter는 변환된
authority를 그 연결의 SNI와 결합해 검증합니다. DNS 대소문자, 마지막 root dot 하나, 생략하거나
명시한 443 포트는 같은 대상으로 인정합니다. SNI 누락, 다른 host/port 또는 일반
`Host`와 `:authority`의 동시 사용은 fail-closed로 거부합니다.

HTTP/2의 일반 DATA와 tunnel DATA는 모두 peer connection/stream send window를
예약한 뒤 전송합니다. 입력 WINDOW_UPDATE credit은 제한된 transformer가
보관했거나 목적지 쓰기가 완료된 바이트에 대해서만 돌려줍니다.

HTTP/2는 양방향에서 독립적이고 크기가 제한된 HPACK 상태를 유지하며, 변환한
헤더는 동적 테이블 결합 없이 다시 인코딩합니다.

HTTP/1.1, HTTP/2, HTTP/3는 제한된 gzip, zlib `deflate`, Brotli
decoder와 encoder를 공유합니다. 증분 스트리밍 API는 메시지마다 codec chain
상태를 따로 유지하므로 임의의 HTTP chunk/DATA 분할에서도 압축 상태를
초기화하지 않으며 전체 body를 한 번에 보관할 필요가 없습니다. coding 깊이,
입력·출력 크기, 확장 비율, checksum, 잘린 입력, 연결 수 제한을 넘으면 fail
closed합니다.

이 예제는 HTML 기록과, 재작성한 본문의 바이트를 하나도 전달하기 전에 결정을
내려야 하는 정책에 완전한 메시지 변환을 사용합니다. 라이브러리는
`ntl/net/http/http1_stream_transform`, `ntl/net/http2/stream_transform`,
`ntl/net/http3/stream_transform`의 라이브 변환 어댑터도 제공합니다. 이 어댑터는
프레이밍과 codec 상태가 허용하는 즉시 변환한 조각을 전달합니다. 이후 거부하면
stream을 reset하거나 닫지만 이미 보낸 바이트를 회수할 수는 없습니다. HTTP 버전이
아니라 정책의 결정 경계를 기준으로 원자적 어댑터와 라이브 어댑터를 선택하세요.

NTL HTTP/3 계층에는 분할 frame 재조립, 제한된 동적 RFC 9204 QPACK,
RFC 9297 HTTP Datagram과 Capsule framing, HTTP/2·HTTP/3 extended CONNECT
검증, 제한된 WebTransport-over-HTTP/3 parser가 있습니다.

서비스는 `msquic_server`와 `proxy_connection`을 사용하므로 예제 코드가
SETTINGS, QPACK, request stream, WebTransport 상태를 다시 구현하지 않습니다.
raw MsQuic backend는 request, 양방향·단방향 stream, QUIC Datagram,
reliable-reset-at event를 제공합니다. 실제 loopback contract는 TLS 1.3/h3를
협상하고 SETTINGS와 QPACK Extended CONNECT를 교환한 뒤 WebTransport 양방향
stream, 단방향 stream, HTTP Datagram을 전송합니다. 또한 여러 번 쓸 수 있는
stream을 열고 32비트 application error를 draft HTTP/3 범위로 매핑한 뒤,
상대가 session prefix가 보장된 reset을 받는지 검증합니다. pinned msh3는
controlled client/origin fixture에만 남습니다. preview reliable-reset-at API를
사용할 수 없으면 WebTransport capability는 false입니다.

## 보안 경계

WFP application identity는 격리 프로필이 아니라 실행 파일 경로입니다. 정책이
활성화된 동안 같은 실행 파일을 사용하는 모든 프로세스가 범위에 들어가므로
전용 VM에서 시험하고 캡처된 HTML을 민감 정보로 취급해야 합니다.

ECH extension이 보인다는 사실만으로 실제 ECH라고 단정할 수 없습니다.
GREASE도 같은 wire 형태를 사용합니다. 임의 공개 사이트의 ECH를 WFP metadata나
Schannel만으로 복호화할 수 없습니다. matching ECH private configuration과
HPKE 및 inner/outer ClientHello 검증을 소유한 TLS frontend가 필요하며, 없으면
확인된 ECH를 fail closed합니다.

NTL은 certificate pinning을 우회하지 않습니다. 정확한 앱·호스트 조합을
분류하여 inspect, block, bypass 정책을 선택할 수는 있지만 pinned client가
대체 leaf를 신뢰하도록 만들 수는 없습니다.

원격 서버에 대한 mTLS는 TCP와 HTTP/3 경로 모두 명시적인
SNI-to-client-certificate provider로 지원합니다. 선택된 인증서에 접근 가능한
private key가 없거나 필수 identity를 찾지 못하면 fail closed합니다.

upstream Chromium의 QUIC proof verifier는 유효한 private-CA chain도
`is_issued_by_known_root=false`이면 거부할 수 있습니다. Windows root store나
Edge `CACertificates` 정책은 chain을 유효하게 만들 수 있지만 그 private
anchor를 Chromium QUIC의 `known root`로 바꾸지는 않습니다. 따라서 기본
브라우저 경로는 설정을 바꾸지 않고 UDP 443을 차단하여 TCP fallback을
검사합니다.

이 제약은 모든 상용 제품이 HTTP/3를 검사할 수 없다는 뜻이 아닙니다. 관리형
클라이언트나 브라우저 전용 통합은 일반 private CA와 WFP redirect만으로는
얻을 수 없는 신뢰 결정을 소유할 수 있습니다. 이 예제의 NTL 관리형
클라이언트가 바로 그 구조를 보여 줍니다.

제품 TLS contract는 관리형 identity 선택, 실제 ECH frontend가 소유한 plaintext
인계, 명시적인 원격 서버 mTLS 선택, 크기가 제한된 감사를 제공합니다. 하지만
private ECH configuration을 제공하거나 endpoint pinning을 우회할 권한은
제공하지 않습니다.

구성된 frontend가 없는 임의 공개 ECH, pinning 우회, 관리되지 않는
client-certificate 자동 선택, 443 이외 origin, 수정되지 않은 일반 브라우저의
투명 HTTP/3 MITM, 그 private-CA 경로를 통한 브라우저 WebTransport는 이
runtime이 지원한다고 주장하지 않습니다. raw MsQuic WebTransport loopback은
실제 transport 시험이지만 Chromium이 enterprise/private CA를 QUIC에
허용한다는 증거는 아닙니다.
