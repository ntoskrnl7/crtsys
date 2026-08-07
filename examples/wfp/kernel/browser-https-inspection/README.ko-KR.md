# 커널 브라우저 HTTPS 검사

[English](./README.md) · [WFP 예제](../../README.ko-KR.md)

이 예제는 사용자 모드
[`browser-https-inspection`](../../user/browser-https-inspection/)의 커널
데이터 경로 대응 예제입니다. 드라이버가 TLS를 종료하고 HTTP를 파싱하며,
크기가 제한된 변환과 허용·차단 정책을 적용하고, 검증된 원본 서버에
연결한 뒤 개인정보 범위가 제한된 캡처 레코드를 발행합니다.

## 프로세스 경계

제품 예제와 관리형 런타임 acceptance를 다음처럼 분리합니다.

- `crtsys_wfp_kernel_browser_https_inspection.sys`는 커널 TCP/TLS,
  HTTP/1.1, HTTP/2, QUIC/HTTP/3, WebSocket, WebTransport, 콘텐츠 디코딩,
  변환, 캡처 데이터 경로를 소유합니다.
- `crtsys_wfp_kernel_browser_https_inspection_controller.exe`는 드라이버,
  인증서 identity, 애플리케이션 범위 WFP 정책을 관리합니다. 일반 모드에서는
  이미 실행 중인 브라우저만 관찰합니다. 브라우저 실행, HTTP 트래픽 생성,
  통제된 원본 서버 실행, acceptance PASS 판정을 하지 않습니다.
- `crtsys_wfp_kernel_browser_https_inspection_acceptance.exe`는
  `BUILD_TESTING=ON`일 때만 빌드됩니다. 소스는
  `test/wfp/runtime/fixtures/kernel/browser-https-inspection`에 있으며,
  결정적인 원본 서버와 관리형 클라이언트, 트래픽 생성, 증거 판정, PASS를
  소유합니다.

acceptance 실행 파일은 같은 디렉터리의 controller 실행 파일을 control-server
모드로 실행합니다. 버전이 있는 named pipe hello 성공이 ready 계약입니다.
WFP 세션과 모든 드라이버 IOCTL은 controller 프로세스가 소유합니다.
acceptance는 typed 명령을 보내고 통제된 네트워크 교환만 실행한 뒤 명시적인
stop 명령을 보내며 controller 종료까지 기다립니다. 합쳐진 `_app.exe`나
`--self-test` 모드는 없습니다.

## 지원 범위

| 프로토콜 | 커널 경로 | 검사 동작 | 런타임 증거 |
| --- | --- | --- | --- |
| HTTP/1.1 | 애플리케이션 범위 IPv4/IPv6 TCP redirect, WSK + 커널 Schannel | pipelining, 허용·차단, HTML과 제한된 gRPC 변환, gzip/deflate/Brotli, WebSocket `permessage-deflate` | acceptance의 관리형 IPv4/IPv6 원본과 클라이언트 |
| HTTP/2 | WSK + 커널 Schannel ALPN `h2` | 동시 stream, flow control, GOAWAY, HTTP/gRPC 변환, 압축, Extended CONNECT WebSocket, 미지원 CONNECT fail-close | 관리형 IPv4/IPv6 H2 교환과 evidence gate |
| HTTP/3 | 애플리케이션 범위 양방향 UDP tuple translation, inbox 커널 MsQuic + TLS 1.3 | SETTINGS, 동적 QPACK, multiplexing, stream 단위 차단·reset, gRPC, 압축, strict H3 원본과 증명 기반 H2/H1 fallback | 관리형 QUIC 교환, outbound DATAGRAM_DATA와 reverse OUTBOUND_IPPACKET/network-send telemetry, no-replay와 원본 보안 음성 사례 |
| WebTransport | 커널 HTTP/3 서비스 | Extended CONNECT, stream, datagram, reset, Capsule | 관리형 QUIC acceptance |

원본 보안은 운영체제 체인 검증, 정확한 leaf pin, 클라이언트 인증서 인증,
ALPN 협상, 보안 설정 교체 실패 후 rollback, unknown CA·잘못된 pin·잘못된
클라이언트의 fail-close를 검증합니다. 리소스 시험은 stream, connection,
대기 작업, 요청 버퍼, 원본 할당 한도와 취소·정상 drain을 포함합니다.

HTTP/1·HTTP/2의 큰 wire/frame 저장소는 활성 session quota가 있는 고정 nonpaged
workspace pool에서 lease합니다. 크기가 달라지는 semantic header, body, codec
상태는 제한된 crtsys nonpaged allocation을 사용하며, 이를 하나의 arena라고
설명하지 않습니다. lease 부족, 할당 실패, protocol 상한 위반은 해당 flow를
fail-close하고 정상 stop/drain 뒤 활성 lease 수가 0이 되어야 합니다.

두 listener를 열기 전에 드라이버는 pool의 owning 계약도 직접 검증합니다.
quota 고갈은 fail-close하고, 중복 close는 멱등이며, pool facade보다 lease가
오래 살아도 안전하고, `DISPATCH_LEVEL`에서 마지막 lease를 해제하면 runtime이
소유한 PASSIVE cleanup domain에서 파괴되어야 합니다. 이 결과는 service ABI에
포함되며 controller와 acceptance 실행 파일은 통과하지 않으면 진행하지 않습니다.

WebSocket 양방향 relay는 `kernel::join_bidirectional`을 사용합니다. 어느 한
방향도 따로 동기 대기할 수 없으며, 양쪽 방향 중 하나가 비성공 결과로 끝나면
두 transport를 취소합니다. 정상 WebSocket 종료는 close frame을 전달한 뒤 성공으로
끝나고 양쪽 TLS `close_notify`도 모두 교환하며, 양쪽
relay coroutine이 끝난 뒤에만 부모가 계속됩니다. 따라서 async stream을 재개해야
하는 worker가 같은 flow의 동기 대기에 막히지 않습니다.

캡처 내보내기는 커널 내부 검사보다 더 엄격한 개인정보 경계를 가집니다.
요청 헤더 값, 쿠키, 인증 정보, 경로, 요청 본문은 내보내지 않고 제한된 구조
메타데이터만 기록합니다. 커널이 의미적 Content-Type을 HTML로 명시한 디코딩
응답만 `response.html`로 기록합니다.

## 빌드와 계약 테스트

```powershell
cmake -S examples\wfp\kernel\browser-https-inspection `
      -B artifacts\examples\wfp-kernel-browser-https-inspection -A x64 `
      -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-kernel-browser-https-inspection `
      --config Release
ctest --test-dir artifacts\examples\wfp-kernel-browser-https-inspection `
      -C Release --output-on-failure
```

호스트 CTest는 parser, ABI, 개인정보, evidence 계약을 검증합니다. WFP,
Schannel, 커널 MsQuic 런타임을 실행했다고 주장하지 않습니다.

## 이미 실행 중인 브라우저 관찰

드라이버를 설치하고 시작한 관리자 권한 시험 VM에서 실행합니다.

```powershell
.\crtsys_wfp_kernel_browser_https_inspection_controller.exe `
    "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" `
    .\capture 30
```

첫 번째 인자는 사용자가 이미 실행한 브라우저의 정확한 실행 파일 경로입니다.
controller는 브라우저 설정, 정책, 프로필, 프록시, 인증서 오류 무시 옵션,
프로토콜 플래그를 바꾸지 않습니다. 지정 시간이 지나거나 Ctrl+C를 누르거나
`capture\stop.request`를 만들면 종료합니다. 종료 시 WFP 세션과 임시 검사
identity가 제거됩니다.

일반 Chromium 모드는 애플리케이션 범위 UDP/443을 차단하여 변경하지 않은
브라우저가 검사 가능한 TCP fallback을 사용하게 하며, 그 fallback을 HTTP/3로
표시하지 않습니다. 별도 acceptance 실행 파일이 브라우저 동작을 바꾸지 않고
실제 커널 HTTP/3 경로를 증명합니다. certificate pinning과 클라이언트의 private
CA 제한은 클라이언트 신뢰 정책의 경계입니다.

## 관리형 런타임 acceptance

```powershell
.\crtsys_wfp_kernel_browser_https_inspection_acceptance.exe .\capture
```

선택 인자는 증거 디렉터리입니다. acceptance가 같은 디렉터리의
`crtsys_wfp_kernel_browser_https_inspection_controller.exe`를 직접 찾고
실행하므로 이 명령을 위해 controller를 따로 실행하지 않습니다. controller는
WFP와 드라이버 설정만 소유하고 acceptance는 통제된 트래픽과 검증만
소유합니다. 증거 파일을 flush하고 controller가 stop을 승인한 뒤 정상 종료해야
PASS를 출력합니다.

## 소스 구성

- `driver/`: 커널 TLS/HTTP/QUIC 처리와 캡처
- `app/controller.cpp`: 이미 실행 중인 브라우저를 지속 관찰하는 controller
- `app/control_server.cpp`, `app/managed_policy.cpp`: 런타임 acceptance용 typed
  IPC 명령 서버와 controller 소유 정책. 트래픽 생성과 PASS 로직은 없음
- `test/wfp/runtime/fixtures/kernel/browser-https-inspection/`: 통제된 원본과
  클라이언트, 프로토콜 시나리오, evidence gate, acceptance main
- `test/`: 순수 호스트 계약 테스트

임의의 1xx 응답, trailer rewrite, ECH 키 배포, 애플리케이션별 pinning 예외는
이 제한된 예제 밖의 제품 정책이 필요합니다.
