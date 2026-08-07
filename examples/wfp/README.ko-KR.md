# NTL WFP 예제

[WFP 입문 가이드](../../docs/ntl/wfp-guide.ko-KR.md) ·
[English](./README.md)

예제는 application content를 검사하고 변환하는 runtime에 따라 나뉩니다.
`user` 예제는 controller 또는 proxy 프로세스가 protocol 정책을 실행합니다.
`kernel` 예제는 핵심 판단이나 변환을 드라이버에서 직접 실행하고 앱은 정책
설정과 telemetry만 담당합니다. 양쪽에서 재사용하는 protocol 구현은 예제의
`common`이 아니라 `<ntl/net/...>`에 둡니다.

application-content 예제는 같은 목적을 두 runtime에서 비교할 수 있도록 짝을
맞췄습니다.

| 목적 | 사용자 모드 정책 | 커널 직접 정책 | 공통 의미 검증 범위 |
|---|---|---|---|
| TCP 메시지 허용·차단 | [`user/tcp-content-filter`](./user/tcp-content-filter/) | [`kernel/tcp-content-filter`](./kernel/tcp-content-filter/) | IPv4/IPv6, 같은 bounded sample-record framing, 구조화된 허용·차단·malformed 정책과 policy 제거 후 복원 |
| UDP datagram 허용·차단 | [`user/udp-content-filter`](./user/udp-content-filter/) | [`kernel/udp-content-filter`](./kernel/udp-content-filter/) | IPv4/IPv6 완전한 datagram, 같은 구조화 record, 허용·차단·malformed와 복원. clone/reinject 및 비동기 queue는 user verdict 경로에만 필요 |
| TCP endpoint | [`user/connect-redirect`](./user/connect-redirect/) | [`kernel/connect-redirect`](./kernel/connect-redirect/) | IPv4/IPv6 원래 tuple과 opaque redirect record, loop 없는 original connect, bounded 양방향 coroutine relay, 원본 우회 없는 unavailable-peer fail-closed와 복원 |
| TLS/HTTP 내용 | [`user/tls-inspection-proxy`](./user/tls-inspection-proxy/) | [`kernel/tls-inspection-proxy`](./kernel/tls-inspection-proxy/) | 검증되는 Schannel 양단, SNI identity, ALPN HTTP/1.1·HTTP/2, bounded framing/HPACK, 요청·응답 변환, 허용·차단과 IPv4/IPv6 원래 목적지 |
| 브라우저 HTML | [`user/browser-https-inspection`](./user/browser-https-inspection/) | [`kernel/browser-https-inspection`](./kernel/browser-https-inspection/) | HTTP/1.1, HTTP/2, managed HTTP/3, bounded codec과 의미 기반 내용 정책. 기존 브라우저 모드는 실행·재설정하지 않음 |
| HTTP/3/QPACK | [`user/http3-inspection`](./user/http3-inspection/) | [`kernel/http3-inspection`](./kernel/http3-inspection/) | 실제 QUIC/TLS 1.3, bounded dynamic QPACK resume/ack, gzip/deflate/Brotli, Extended CONNECT, WebTransport stream·Datagram·capsule·reliable reset과 정확한 종료 |

쌍을 이루는 예제는 비슷하게 복사한 정책 두 개를 유지하지 않습니다. 각 쌍은
[`shared`](./shared/)의 정책 또는 record 계약 하나를 함께 사용하며, transport,
scheduler와 실행 주소 공간만 다릅니다. 한쪽이 공통 정책을 더 이상 사용하지
않으면 저장소 parity 계약이 실패합니다.

`ale-connect-block`, `async-inspection`, `bind-redirect`, `datagram-proxy`,
`flow-monitor`, `stream-edit`, `specialized-observation`은 WFP callout primitive와
driver 수명 자체를 가르치는 커널 전용 예제이므로 억지로 빈 user 사본을 만들지
않습니다. 반대로 위 표의 user 예제는 정책 service/offload를, kernel 예제는
같은 의미의 직접 처리와 bounded 제한을 보여줍니다.

일곱 예제가 커널 전용인 이유도 구체적입니다. `ale-connect-block`은 가장 작은
classify/action, `async-inspection`은 pended operation과 retained packet,
`bind-redirect`는 writable bind request, `datagram-proxy`는 NBL clone/reinject,
`flow-monitor`는 WFP flow context, `stream-edit`는 stream continuation/injection,
`specialized-observation`은 IPsec·MAC/frame·vSwitch·endpoint-close·fast-layer
capability를 다룹니다. 이들은 user data plane의 대안이 아니라 커널 WFP
mechanism 자체이므로 user 디렉터리를 추가해도 같은 드라이버의 controller만
중복됩니다.

처음에는 [`ale-connect-block`](./kernel/ale-connect-block/README.ko-KR.md)부터
보십시오. 커널 callout이 선택한 outbound IPv4 TCP 연결 하나를 차단하고,
동적 정책을 제거하면 같은 연결이 다시 허용되는 가장 작은 예제입니다.

선택한 TCP 연결을 사용자 모드 proxy로 보내려면
[`connect-redirect`](./user/connect-redirect/README.ko-KR.md)를 사용합니다. 원래
목적지와 WFP redirect record를 보존하고 IOCP coroutine으로 양방향 byte
stream을 중계합니다.

proxy data plane까지 드라이버에 두려면
[`kernel/connect-redirect`](./kernel/connect-redirect/README.ko-KR.md)를
사용합니다. `<ntl/net/kernel/wsk_redirect>`가 accepted WSK socket에서 원래
tuple과 opaque redirect record를 캡처하고 outbound WSK connect 전에 record를
적용한 뒤, bounded 커널 coroutine으로 IPv4/IPv6 양방향 stream을 중계합니다.

TLS 평문 내용까지 검사하려면
[`tls-inspection-proxy`](./user/tls-inspection-proxy/README.ko-KR.md)를 사용합니다.
TLS와 인증서 정책은 사용자 모드 Schannel 계층에 있고, bounded HTTP/1.1과
ALPN `h2` 모두 실제 client-proxy-origin 경로에서 허용·차단합니다. HTTP/2는
요청·응답 변환도 검증합니다. 커널 callout은 연결을 검사 경로로 강제할 뿐
평문이나 TLS key를 받지 않습니다.

브라우저 HTTPS HTML을 계속 기록하려면
[`browser-https-inspection`](./user/browser-https-inspection/README.ko-KR.md)을
사용합니다. 선택한 브라우저 실행 경로만 redirect하고 HTTP/1.1·HTTP/2 HTML,
gzip/deflate/Brotli와 협상된 WebSocket `permessage-deflate`를 bounded하게
검사합니다. 런타임 스크립트는 정확한 실행 파일 경로로 이미 실행 중인
브라우저만 계속 관찰하며, profile을 만들거나 브라우저를 실행·종료하거나
명령행 flag와 브라우저 설정을 변경하지 않습니다.

동일한 controlled HTTP/1.1·HTTP/2 판단을 드라이버의 WSK와 Schannel에서,
HTTP/3 판단을 커널 MsQuic에서 직접 실행하려면
[`kernel/tls-inspection-proxy`](./kernel/tls-inspection-proxy/)와
[`kernel/browser-https-inspection`](./kernel/browser-https-inspection/)을 사용합니다.
후자의 별도 acceptance 실행 파일은 H1/H2/H3 허용·차단을 한 번에 검증합니다.
예제 controller는 정책과 드라이버 제어만 소유하고, managed client·origin과
판정 코드는 `test/wfp/runtime/fixtures`에 둡니다. 기본 브라우저 모드는 정확한
경로의 이미 실행 중인 브라우저를 관찰하며 명령행 flag나 브라우저 설정을
바꾸지 않습니다.

QUIC terminator 위의 HTTP/3 검사 경계는
[`http3-inspection`](./user/http3-inspection/README.ko-KR.md)을 참고하십시오.
결정적 backend가 임의로 나뉜 복호화 stream을 전달하면 NTL이 HTTP/3 frame,
크기가 제한된 dynamic QPACK blocked-stream 재개/확인 응답,
gzip/deflate/Brotli, Extended CONNECT와 WebTransport stream·Datagram·capsule·
reliable reset을 검사합니다. TLS 1.3, 패킷 복구와 stream 수명은 제품 QUIC
provider의 책임입니다. 이는 앱이 관리하는 endpoint 계약이며, 변경하지 않은
Chromium 브라우저가 임의 origin에 대한 사설 CA identity를 받아들인다는 뜻은
아닙니다.

실제 kernel MsQuic NMR provider에서 같은 dynamic QPACK, codec, Extended
CONNECT/WebTransport 의미와 내용 기반 200/403, 연결 수명까지 검증하려면
[`kernel/http3-inspection`](./kernel/http3-inspection/)을 사용합니다.

사용자 모드에서 내용으로 허용·차단하는 예제는 전송 방식별로 나뉩니다.

- [`udp-content-filter`](./user/udp-content-filter/README.ko-KR.md)는 완전한 UDP
  datagram 하나를 판단하여 보관한 clone을 재주입하거나 폐기합니다.
- [`tcp-content-filter`](./user/tcp-content-filter/README.ko-KR.md)는 byte stream에서
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

Visual Studio/NuGet에서는 crtsys WDM 진입점 속성 페이지에서 **NTL WFP**를
선택합니다. 진입점, `fwpkclnt.lib`, package의 커널 zlib/Brotli backend가 자동으로
연결됩니다. CMake 프로젝트는 `WFP NTL`을 사용하며, 직접 커널 content codec과
고정된 MsQuic header는 WFP 문서의 선택적 target으로 명시해서 연결합니다. 어떤
빌드 단계도 드라이버나 MsQuic provider를 설치하지 않습니다.

## Visual Studio 솔루션

`user`와 `kernel` 아래의 모든 예제 디렉터리에 체크인된 `*_vs.sln`이
있습니다. 이 솔루션을 열면 예제의 WDM callout 드라이버와 controller 또는
policy service를 함께 빌드할 수 있습니다. 사용자 모드 browser 예제에는 분리된
HTTP/3 proxy service 프로젝트도 들어 있습니다. 모든 드라이버 프로젝트는
`NtlWfp`를 선택하며, HTTP/3 응용 프로그램 프로젝트는 고정된 native MsQuic
package를 복원하고 실행 파일 옆에 `msquic.dll`을 복사합니다.

예를 들면 다음과 같습니다.

```powershell
msbuild .\examples\wfp\kernel\ale-connect-block\crtsys_wfp_ale_connect_block_vs.sln `
  /restore /p:Configuration=Debug /p:Platform=x64
```

솔루션에는 예제를 설명하는 driver와 controller/service target만 넣었습니다.
트래픽을 만드는 acceptance 실행 파일과 세부 contract 실행 파일은 기존처럼
`test/wfp`에 두고 CMake로 빌드하므로, 예제 솔루션에 테스트 전용 프로젝트가
뒤섞이지 않습니다. `.vcxproj`와 솔루션 파일은 하나의 검토 가능한 manifest에서
생성되며 다음 명령으로 일치 여부를 검사합니다.

```powershell
.\scripts\examples\Generate-WfpVisualStudioProjects.ps1 -Check
.\scripts\ci\Test-CrtSysWfpExampleProjects.ps1
```

위 명령은 저장소 루트에서 실행합니다. 공개 CMake target이나 source 목록이
바뀌면 생성된 프로젝트를 직접 고치지 말고 generator manifest를 수정해야 합니다.
솔루션 빌드는 출력물을 컴파일하고 package할 뿐이며 HOST에 드라이버를 설치하거나
로드하지 않습니다.
