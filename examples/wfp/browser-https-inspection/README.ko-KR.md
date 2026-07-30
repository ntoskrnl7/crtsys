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

wrapper는 격리 프로필을 사용하지만 QUIC 비활성화, 프로토콜 강제, 인증서
오류 무시, ECH 정책 변경 인자를 브라우저에 전달하지 않습니다. 브라우저
경로에서만 예제 inspection CA를 VM의 신뢰 저장소에 임시 등록하고 종료할 때
제거합니다.

TCP 경로는 WFP가 보존한 원래 목적지를 복구하고, 제한된 크기의 ClientHello에서
SNI를 읽어 짧은 수명의 호스트별 leaf를 발급합니다. 브라우저 TLS를 종료한 뒤
정상 인증서 검증을 사용하는 origin TLS 연결을 만들고 HTTP/1.1, HTTP/2,
WebSocket을 검사합니다.

## 관리형 HTTP/3 경로

관리형 클라이언트 경로는 WFP 브라우저 정책과 명확히 분리되어 있습니다.
NTL 클라이언트가 명시적인 loopback 검사 endpoint에 연결하되 원래 SNI와
`:authority`를 그대로 사용합니다. 검사 CA는 애플리케이션 메모리 안에서만
검증하며 Windows나 브라우저 신뢰 저장소에 쓰지 않습니다. 클라이언트와
검사 endpoint 사이의 downstream 연결은 실제 QUIC/TLS 1.3 및 HTTP/3입니다.

선택적인 WFP UDP/443 관리형 리디렉션에서는 원래 목적지 컨텍스트를 만들지
않습니다. 관리형 프로토콜이 원래 SNI와 `:authority`를 이미 소유하고 H3
프록시가 그 컨텍스트를 조회하지 않기 때문입니다. 반대로 일반 브라우저 TCP
리디렉션은 accept된 소켓에서 원래 목적지를 복구해야 하므로 컨텍스트를
계속 사용합니다.

관리형 프록시는 origin HTTP/3를 먼저 시도합니다. 외부 QUIC이 transport,
연결 또는 timeout 오류로 실패한 경우에만 정상 인증서 검증을 사용하는
TLS/TCP로 재시도할 수 있습니다. 실제 upstream은 요청마다 `h3`, `h2` 또는
`http/1.1`로 기록합니다. 인증서, mTLS, 요청 검증 실패는 fallback 사유가
아니며 그대로 실패합니다.

외부망의 UDP/443 허용 여부와 분리된 결정적 acceptance도 제공합니다.
`Start-ControlledHttp3EndToEnd.ps1`은 다음 경로를 loopback 안에서 실제
msh3/MsQuic 연결로 실행합니다.

```text
NTL client -- H3/TLS 1.3 --> inspection proxy -- H3/TLS 1.3 --> controlled origin
```

검사 CA와 origin CA를 각각 정확한 private anchor로 메모리에서 검증합니다.
드라이버, 브라우저, root 저장소 등록, 외부 DNS, 재부팅은 사용하지 않습니다.
identity와 gzip/deflate/Brotli HTML, 잘못된 CA와 호스트, upstream body 제한,
동시 요청과 정상 종료를 한 번에 검사합니다.

`events.log`에는 호스트, 실제 협상 프로토콜, 상태 코드, 콘텐츠 형식, 인코딩,
복원된 본문 크기를 기록합니다. `.html`은 서버 응답 본문이며 JavaScript 실행
뒤의 DOM snapshot은 아닙니다. 요청 헤더와 쿠키는 로그에 남기지 않습니다.

## 소스 구성

- `main.cpp`: 명령행 해석
- `browser_runtime.*`: listener, WFP 수명, 공유 IOCP 연결 task registry,
  인증서, task drain 종료
- `browser_policy.*`: 앱 범위 TCP/UDP WFP 필터
- `browser_proxy.*`: TCP ClientHello, Schannel, ALPN 프로토콜 분기
- `http1_inspection.*`: 제한된 HTTP/1.1 및 WebSocket relay
- `http1_inspection_support.*`: 직접 테스트할 수 있는 요청 재작성, 콘텐츠
  복원, WebSocket 확장 협상
- `http2_inspection.*`: HTTP/2 frame, HPACK, stream 정책
- `http3_inspection.*`: 제한된 HTTP/3 응답 검사 정책
- `http3_controlled.cpp`: 결정적 H3 origin과 H3 upstream acceptance
- `http3_live_proxy.*`: 범용 검사 프록시에 연결하는 msh3 server 및 WinHTTP
  origin adapter
- `http3_origin.*`: strict-H3 및 transport-fallback origin 정책
- `managed_http3_client.cpp`: NTL 관리형 HTTP/3 클라이언트 예제
- `bidirectional_relay.hpp`: 구조화된 비동기 relay 완료, 상대 방향 취소,
  task drain
- `browser_log.*`: event 및 HTML 출력

범용 구현은 `include/ntl`의 `http3_backend`, `http3_msh3_backend`,
`http3_msh3_client`, `http3_inspection_proxy`,
`http3_standard_inspection_proxy`, `http3_qpack`, `http_datagram`,
`http_extended_connect`, `webtransport_http3`, `tls_inspection_frontend`에
있습니다. HTTP/3 검사 프록시는 요청을 검증하고 SNI와 `:authority`를
결속하며 제한된 디코딩과 typed 요청/응답 정책을 적용합니다. origin
adapter에는 검증을 마친 요청만 전달합니다.

## 빌드와 실행

```powershell
cmake -S examples\wfp\browser-https-inspection `
      -B artifacts\examples\wfp-browser-https-inspection -A x64
cmake --build artifacts\examples\wfp-browser-https-inspection `
      --config Release
```

실행 파일의 모드는 다음과 같습니다.

```text
crtsys_wfp_browser_https_inspection_app.exe <browser.exe> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --managed-http3-proxy <listen-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --controlled-http3-e2e <proxy-port> <origin-port> <log-directory> [duration-seconds]
crtsys_wfp_browser_https_inspection_app.exe --http3-spki-proxy <server-name> <listen-port> <log-directory> <duration-seconds>
crtsys_ntl_managed_http3_client.exe <https-url> <output-file> [<inspection-port> <inspection-ca.cer>]
```

VM에서 일반 브라우저 경로를 실행하는 방법은 다음과 같습니다.

```powershell
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Urls @('https://www.google.com/') `
    -RequireQuicBlockedFallback `
    -LogDirectory (Join-Path (Get-Location) 'browser-log') `
    -DurationSeconds 90
```

`-RequireQuicBlockedFallback`은 브라우저 설정 인자가 아니라 자동화
assertion입니다. 실행 중인 WFP 객체와 정확한 필터 조건을 검증하고, 커널에서
해당 앱의 UDP/443 classify와 block이 실제로 한 번 이상 관찰되어야 하며, Edge
NetLog에서 대상 호스트로 직접 도달한 QUIC 세션이 없어야 합니다. 동시에 TCP로
검사된 HTML도 있어야 PASS입니다. QUIC classify가 한 번도 없으면 차단 성공으로
오판하지 않고 검증 불충분으로 실패합니다. 로그 디렉터리에는 bounded 정책
진단, 커널 텔레메트리, NetLog 판정 파일이 남습니다.

관리형 HTTP/3 경로에는 드라이버, 브라우저 실행, 브라우저 인자 또는 신뢰
저장소 변경이 필요 없습니다.

```powershell
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot (Get-Location).Path `
    -Url 'https://www.google.com/' `
    -LogDirectory (Join-Path (Get-Location) 'managed-http3-log')
```

이 wrapper는 명시적 loopback endpoint를 시작하고 임시 CA를
`crtsys_ntl_managed_http3_client.exe`에만 전달합니다. 그 뒤 downstream
`h3`와 HTML 검사 결과를 확인합니다.

외부 네트워크 없이 양쪽 H3를 함께 검증하려면 드라이버 없는 controlled
wrapper를 실행합니다.

```powershell
.\Start-ControlledHttp3EndToEnd.ps1 `
    -PackageRoot (Get-Location).Path `
    -Concurrency 8
```

VM packaging과 전후 상태 검사는
`test/wfp/runtime/https-live/CONTROLLED-HTTP3-README.ko-KR.md`에 설명되어
있습니다.

`Start-BrowserHttp3SpkiDiagnostic.ps1`는 격리된 transport 진단 도구로만
남겨 둡니다. 시험 traffic을 loopback에 매핑하고 폐기 가능한 브라우저
프로세스에 정확한 임시 SPKI 하나를 전달합니다. 일반 WFP 경로도 아니고
투명한 interception을 입증하는 도구도 아닙니다.

## 지원 범위와 제한

TCP 경로는 Schannel이 협상하는 TLS 1.2/1.3, HTTP/1.1 HTML, 검증된
`permessage-deflate`가 있는 RFC 6455 WebSocket, multiplexed HTTP/2 HTML을
지원합니다. HTTP/1.1과 HTTP/2는 제한된 gzip, zlib `deflate`, Brotli
decoder를 공유합니다. coding 깊이, 입력·출력 크기, 확장 비율, checksum,
잘린 입력, 연결 수 제한을 넘으면 fail closed합니다.

NTL HTTP/3 계층에는 분할 frame 재조립, 제한된 동적 RFC 9204 QPACK,
RFC 9297 HTTP Datagram과 Capsule framing, HTTP/2·HTTP/3 extended CONNECT
검증, 제한된 WebTransport-over-HTTP/3 parser가 있습니다.

현재 고정된 msh3 server backend는 일반 요청·응답 callback은 제공하지만
raw 양방향·단방향 stream과 QUIC Datagram callback은 제공하지 않습니다.
따라서 live extended CONNECT, HTTP Datagram, WebTransport는 backend
capability에서 지원하지 않는다고 보고합니다. NTL parser 구현이 있다는 것과
현재 예제 transport에서 실제로 전달할 수 있다는 것은 구분합니다.

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

origin mTLS는 TCP와 HTTP/3 경로 모두 명시적인 SNI-to-client-certificate
provider로 지원합니다. 선택된 인증서에 접근 가능한 private key가 없거나
필수 identity를 찾지 못하면 실패합니다.

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

임의 ECH, pinning 우회, 관리되지 않는 client-certificate 자동 선택, 443 이외
origin, 수정되지 않은 일반 브라우저의 투명 HTTP/3 MITM, 현재 msh3 backend의
live WebTransport는 이 runtime이 지원한다고 주장하지 않습니다.
