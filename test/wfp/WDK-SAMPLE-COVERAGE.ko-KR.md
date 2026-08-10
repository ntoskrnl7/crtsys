# WDK WFP 샘플 적용 범위

이 문서는 재사용 가능한 메커니즘을 다음과 같이 매핑합니다.
`Windows-driver-samples/network/trans` ~ `ntl::wfp`. 보장 범위는 다음을 의미합니다.
메커니즘에는 형식화된 API와 컴파일 계약이 있습니다. 런타임 상태가 명시됩니다.
별도로; 컴파일된 고급 경로는 런타임 테스트된 것으로 보고되지 않습니다.

## 적용 범위 매트릭스

| Microsoft 샘플 | 재사용 가능한 메커니즘 | NTL 인터페이스 | 증거/상태 |
| --- | --- | --- | --- |
| `ddproxy` | ALE 흐름 검색, 형식화된 흐름별 프록시 상태, 데이터그램 분류, NBL 복제, 차단/흡수, 비동기 재주입 | `ale_flow_established_v4/v6`, `datagram_data_v4/v6`, `flow_target`, `add_flow_context`, `cloned_packet`, `transport_injector` | `/W4 /WX`; IPv4/IPv6 종단 간 UDP 리디렉션이 Driver Verifier에서 각각 20회 통과했습니다. 로드 시 NBL 픽스처는 여러 MDL에 걸친 UDP 필드 편집도 검증합니다. |
| `inspect` | ALE 작업 보류, 전송 패킷 보존, 비동기 작업자 판정, 복제/재주입, 언로드 종료 대기 | `pended_operation`, `referenced_packet`, `cloned_packet`, 런다운을 지원하는 네트워크/전송 인젝터 | `/W4 /WX`; IPv4/IPv6 지연 허용·차단과 정책 제거 후 복구가 Driver Verifier에서 각각 20회 통과했습니다. |
| `msnmntr` | 흐름 확립 관찰, 스트림 흐름 컨텍스트, 관찰 전용 스트림 분류, 흐름 삭제 정리 | 흐름 확립/스트림 태그, `inspection_filter_builder`, `add_stream`, `flow_target`, `stream_event` | `/W4 /WX`; IPv4/IPv6 흐름·스트림 텔레메트리가 Driver Verifier에서 20회 통과했습니다. 별도 커널 계약 드라이버가 제한된 코루틴 판독기의 조각화, 시간 초과, 취소, EOF, 경쟁 판독기 및 한도 경로를 검증합니다. |
| `stmedit` 인라인 | 스트림 바이트 복사, 바이트 범위 허용/차단, 추가 데이터 요청, 연결 종료 | `stream_data_view`, `scatter_view`, `scan_bytes`, `stream_result`, `stream_filter_builder`; 사용자 모드 `io_completion_context`, `async_socket` | `/W4 /WX`; 분할 전송 치환과 정책 제거 후 복구가 Driver Verifier에서 20회 통과했습니다. 컨트롤러의 실제 페이로드 경로는 IOCP 코루틴의 정확한 읽기/전체 쓰기를 사용하며 취소와 불완전 EOF 자체 검사도 수행합니다. |
| 사용자 모드 콘텐츠 판정 | 완전한 데이터그램 보존, 애플리케이션 지정 TCP 프레이밍, 인바운드 지연/재개, 제한된 커널-사용자 전달, 형식화된 허용/차단 결과, 시간 초과와 큐 역압 | `content_view`, `udp_datagram_view`, `tcp_message_view`, `framing`, NTL RPC 신뢰성 알림, `cloned_packet`, `transport_injector`, `stream_continuation` | `/W4 /WX`; IPv4/IPv6 UDP와 샘플 프로토콜 TCP의 허용·차단 및 복구가 Driver Verifier에서 20회 통과했습니다. 잘못된 형식, 늦은 응답, 시간 초과 및 할당량 초과는 실패 시 차단됩니다. |
| `stmedit` 대역 외 | 지연/재개, 스트림 데이터 복제, 치환 주입, 별도 스트림 복제본 정리 | `stream_result::defer`, `stream_injection_site`, `cloned_stream_data`, `stream_injector` | `/W4 /WX`; 지연된 복제/재개, 분할 경계의 가변 길이 치환 및 제한된 사용 중 처리가 Driver Verifier에서 3회 통과했습니다. |
| `WFPSampler` 기본 동작 시나리오 | 계층 등록, 공급자/하위 계층/콜아웃/필터 그래프, 허용/차단/계속/흡수 | 형식화된 키/계층, `callout_driver`, `terminating_decision`, `arbitration_decision`, `void` 검사 콜백, 트랜잭션 관리, `policy_session`, `policy_manifest` | 대표 `ALE_AUTH_CONNECT_V4` 경로는 런타임과 Driver Verifier로 검증했습니다. 영구 정책 조정, 컨트롤러 종료 후 유지, 상태 확인 및 명시적 제거도 런타임으로 검증했습니다. |
| `WFPSampler` 패킷/흐름/스트림 시나리오 | 전송/데이터그램 계층, 흐름 연결, 패킷·스트림 주입 | 위의 패킷/흐름/스트림 API | 재사용 가능한 메커니즘은 컴파일로 검증했습니다. 샘플의 대규모 시나리오 CLI는 재현하지 않습니다. |
| `WFPSampler` 연결 리디렉션 | 쓰기 가능한 `FWPS_CONNECT_REQUEST`, 로컬 프록시 PID/포트, 리디렉션 루프 추적, 원래 대상 컨텍스트, 수락 소켓 리디렉션 레코드 전달, 양방향 릴레이 | `ale_connect_redirect_v4/v6`, `connect_redirector`, `local_proxy_target`, `connect_redirect_filter_builder`, `redirected_connection`; 사용자 모드 `async_socket`; 커널 `wsk_redirected_connection`, `async_transport_stream` | `/W4 /WX`; 사용자 경로는 Driver Verifier에서 3회 통과했습니다. 커널 픽스처도 새 충돌/덤프나 Verifier 설정 변경 없이 동일 부팅 VM에서 3회 통과했지만, 이 런타임 전용 실행을 Verifier 대상 결과로 주장하지는 않습니다. |
| `WFPSampler` 바인드 리디렉션 | 쓰기 가능한 `FWPS_BIND_REQUEST`, 듀얼 스택 주소/포트 재작성, 예약 토큰, 이전 버전 루프 방지 | `ale_bind_redirect_v4/v6`, `bind_redirect_selector`, `local_bind_target_v4/v6`, `bind_redirector`, `bind_redirect_filter_builder` | `/W4 /WX`; IPv4/IPv6 선택 포트 바인딩과 정책 제거 후 임시 포트 복구가 Driver Verifier에서 3회 통과했습니다. |
| 사용자 모드 TLS 검사 프록시 | 연결 리디렉션, 수락 구간 TLS 종단, 원래 대상 TLS 클라이언트 구간, 평문 애플리케이션 프레이밍과 형식화된 정책 | 위 WFP 연결 리디렉션 인터페이스; 사용자 모드 `tls_credentials`, `tls_stream`, `framing`, `inspection` | `/W4 /WX`; Schannel 클라이언트/서버 런타임 계약이 x64/x86에서 통과합니다. 리디렉션된 허용·차단과 정책 제거 후 직접 연결 복구는 고급 VM 게이트에서 검증합니다. |
| 브라우저 HTTPS 검사 | 애플리케이션 범위 TCP 리디렉션, 네이티브 UDP/443 대체 경로 강제, 동적 SNI별 ID, 지속 HTTP/1.1, 다중화 HTTP/2, WebSocket, 제한된 콘텐츠 코덱, 관리형 HTTP/3/WebTransport 검사 | 독립 드라이버/앱 계약의 WFP 및 사용자 모드 TLS/HTTP/QUIC 인터페이스 | `/W4 /WX`; 결정적 HTTP/1.1, HTTP/2 및 관리형 HTTP/3 계약은 `test/wfp/runtime/https-live`의 인터넷 의존 격리 브라우저 검증과 별도로 패키지됩니다. 일반 브라우저의 사설 CA HTTP/3 신뢰 경계를 투명 H3 검사로 보고하지 않습니다. |
| 직접 커널 애플리케이션 콘텐츠 정책 | WSK TCP/UDP, 커널 Schannel, CNG/DER X.509, 커널 zlib/Brotli, 직접 HTTP/1.1·HTTP/2 프레이밍/변환 정책, 커널 MsQuic HTTP/3 | 공유 `ntl::net` 프로토콜 코어를 사용하는 `ntl::net::kernel` 전송/공급자 | `/W4 /WX`; 커널 브라우저 통합 드라이버는 사전 구성된 Driver Verifier 대상으로 동일 부팅 3회를 통과했습니다. HTTP/1.1·HTTP/2·HTTP/3 검사/차단/재작성, 압축, gRPC, WebSocket/확장 CONNECT/WebTransport, IPv4/IPv6 경로, 작업 공간 수명/소진, 연결 변동 및 정상 종료를 검증했고 새 충돌/덤프 없이 Verifier 설정을 보존했습니다. |
| 직접 커널 HTTP/3 | MsQuic NMR 공급자, TLS 1.3/QUIC 수명 주기, SETTINGS, 차단 스트림 재개와 확인 응답을 지원하는 제한된 동적/Huffman QPACK, gzip/deflate/Brotli, 확장 CONNECT, WebTransport 스트림/데이터그램/캡슐/신뢰성 재설정 | `kernel::msquic_provider`, 공유 QUIC/HTTP/3/QPACK/WebTransport 계약 | `/W4 /WX`; 공식 컨트롤러 DLL을 사용한 x64 Debug/Release 빌드와 커널 HTTP/3 경로의 Driver Verifier 3회를 통과했습니다. 독립 `kernel-http3-inspection` 픽스처도 96개 순차 연결을 포함한 격리 로드/실행/언로드 3회를 통과했으며, 이 독립 실행은 Verifier 설정을 보존하지만 Verifier 대상 결과로 주장하지 않습니다. |
| `WFPSampler` 특수 시나리오 | IPsec 정책, MAC/프레임, vSwitch, 이름 확인 캐시, 엔드포인트 종료, 고속 계층 메타데이터 | 형식화된 특수 계층 태그, 기능 범주, 컴파일 계약 | `/W4 /WX`; 엔드포인트 종료 IPv4/IPv6와 인바운드/아웃바운드 MAC 분류가 명시적 MAC 요구 조건 아래 Driver Verifier에서 3회 통과했습니다. 기본 제공 WFP 스위치 확장을 활성화한 Hyper-V vSwitch 분류는 `63/63/51` 마스크로 3회 통과했습니다. 활성 전송 모드 IPsec은 양쪽 피어에서 TCP와 UDP 양방향 Quick Mode SA를 생성했고, 관련 Verifier 실행의 로드/언로드 횟수도 `+1/+1` 증가했습니다. IPsec 계층은 관리 전용이고 고속 계층은 정적 콜아웃 대상이 아닌 검사 전용입니다. |

## ALE 연결 블록 런타임 결과

x64 `ale-connect-block` 드라이버와 컨트롤러를 일회용 Windows 11 x64 VM에서
실행한 결과는 다음과 같습니다.

1. 드라이버 등록 및 임시 정책 설치가 성공했습니다.
2. 선택한 루프백 TCP 연결이 `WSAEACCES`로 인해 실패했습니다.
3. 임시 정책 세션을 닫자 정책이 제거되고 연결이 성공했습니다.
4. 영구 매니페스트는 컨트롤러가 닫힌 뒤에도 유지되었고, 새 엔진 연결에서 정상
   상태를 보고하며 계속 강제 적용되었으며, 명시적 제거에도 성공했습니다.
5. 임시 정책 설치/제거를 반복하는 20회 실행이 통과했습니다.
6. 동일 부팅 수명 주기 실행에서 충돌 이벤트나 덤프가 발생하지 않았고, VM의
   기존 Verifier 대상 목록도 변경되지 않았습니다.

저장소의 acceptance 스크립트와 로그가 이 결과를 반복할 수 있는 증거이며, 공통
ALE 수명 주기와 언로드 경로를 검증합니다.

## 고급 런타임 결과

x64 기준 고급 게이트는 관련 없는 샘플을 빌드하거나 패키징하지 않고 임의의
하위 집합을 선택할 수 있습니다. 한 듀얼 스택 정책 게이트에서는
`datagram-proxy`, `async-inspection`, `flow-monitor`, `udp-content-filter`,
`tcp-content-filter`를 함께 Driver Verifier 아래에서 각각 20회 실행했습니다.
이후 전체 게이트에서는 스트림 편집, 두 리디렉션 형식, TLS 검사 및 특수 관찰을
포함한 고급 샘플 10개를 각각 2회 실행했습니다. 전체 결과는 다음과 같습니다.

1. 정책 샘플 5개 각각에서 컨트롤러 20회 반복과, 전체 샘플 10개의 2회 회귀;
2. 모든 대상에서 검증된 드라이버 로드 및 언로드 1회 이상;
3. 조각화된 UDP NBL/MDL 로드 시 계약;
4. IPv4/IPv6 리디렉션, 지연 판정, 텔레메트리, UDP/TCP 판정 경로와 두 주소
   체계 모두에서 정책 제거 후 복구;
5. 개별 반복 전에 수행하는 UDP 콘텐츠 필터의 잘못된 형식·늦은 응답·시간
   초과·보류 한도 실패 시 차단 자체 검사와 TCP 콘텐츠 필터의 잘못된 형식·
   시간 초과·늦은 허용·흐름 삭제 자체 검사;
6. 충돌/재부팅 오류 이벤트 0건과 새 커널 덤프 0개;
7. 동일 부팅 제품군 실행 전후에 운영자가 준비한 Driver Verifier 구성이
   변경되지 않았음.

이전 기반 소스 상태에서는 WFP 컴파일/예제 프로젝트 13개를 x64와 ARM64의
Debug 및 Release 구성으로 빌드했습니다. x64 의미 계약 CTest 8개는 두 구성에서
모두 통과했습니다. 새로 추가된 한 쌍의 커널 예제는 이 과거 개수나 런타임
결과에 포함되지 않습니다. 이전 게이트의 최종 동일 부팅 VM 감사에서는 WFP
서비스, 컨트롤러 프로세스, 테스트 인증서, 프로젝트 WFP 정책, 충돌 이벤트 또는
새 덤프가 발견되지 않았으며, 의도하지 않은 WFP 서비스나 정책도 남지 않았습니다.

별도의 확장 게이트에서는 stream-edit의 지연 복제/재개, 가변 길이 치환 및 IOCP
코루틴 read/write/cancel/EOF 계약, connect-redirect의 IPv4/IPv6 코루틴 릴레이,
IPv4/IPv6 bind redirect, 그리고 TLS 검사 프록시의 두 Schannel 구간, 복호화된
허용/차단 판정, 변경되지 않은 신뢰 저장소, `close_notify` 및 직접 TLS 복구가
통과했습니다. 이후 브라우저 HTTP/3 Verifier 실행에서 드라이버를 즉시 언로드할
때 WFP 소유 원래 대상 컨텍스트 하나가 아직 살아 있는 문제가 드러났습니다. 관리형
UDP 튜플 변환 경로는 이 connect-redirect 컨텍스트를 할당하지 않습니다.
FLOW_ESTABLISHED가 원래 튜플을 보관하고 DATAGRAM_DATA가 제한된 양방향
복제/재작성/재주입을 수행합니다. 이후 같은 외부 QUIC 유휴 실패에서도 새 충돌
이벤트나 덤프 없이 Verifier 아래에서 정상 언로드되었습니다. 일반 TCP 프록시
경로는 계속 이 컨텍스트를 요청합니다.

현재 직접 커널 게이트는 통합 브라우저 드라이버를 운영자가 미리 구성한 Verifier
대상으로 3회 실행했습니다. 각 반복에서 HTTP/1.1, HTTP/2, 로컬 관리형 HTTP/3
정책 및 변환 경로, 작업 공간 소유권/소진, 연결 변동 및 정상 종료 대기를
검증했습니다. 별도의 3회 런타임 게이트에서는 더 작은 커널 TLS 프록시의 듀얼
스택 리디렉션 레코드, 두 Schannel 구간, HTTP/1.1·HTTP/2 허용/차단/재작성,
잘못된 형식과 시간 초과의 실패 시 차단 처리, 제한된 캡처 및 정리를 검증했습니다.
두 게이트 모두 새 충돌 이벤트나 덤프가 없었고 Verifier 설정이 바이트 단위로
동일했습니다. 이 두 실행에서 Verifier 대상이라고 확인한 것은 통합 브라우저
드라이버뿐입니다.

독립형 커널 HTTP/3 게이트도 격리된 드라이버 로드/실행/언로드 3회를 통과했습니다.
각 반복은 순차 연결 96개, IPv4/IPv6 WFP 게이트, TLS 1.3, 동적 QPACK 재개와
확인 응답, gzip/deflate/Brotli, 확장 CONNECT/WebTransport, 정책 제거와 복구,
콜아웃을 사용할 수 없을 때의 실패 시 차단, 제한된 캡처 및 정상 언로드를
실행했습니다. 새 충돌 이벤트나 덤프가 없었고 Verifier 설정도 변경되지 않았지만,
이는 런타임 전용 결과이며 독립형 드라이버가 Verifier 대상으로 구성되었다는
뜻은 아닙니다.

현재 VM 픽스처는 VM을 시작, 재설정, 되돌리기 또는 재부팅하지 않으며 Driver
Verifier도 변경하지 않습니다. 운영자가 먼저 선택한 모든 드라이버를 Verifier
대상으로 구성하고, 필요한 시작 정책으로 일회용 게스트를 수동 부팅한 뒤, 의도적인
일회용 게스트 센티널을 만듭니다. 실행기는 `verifier /query`와
`verifier /querysettings`를 읽고 동일 부팅 제품군을 실행합니다. 선택한 드라이버의
로드/언로드 횟수가 모두 증가하고 설정은 바이트 단위로 동일해야 합니다. 실행별
증분은 `verifier-load-unload-evidence.json`에 기록하므로 이전 누적 횟수로 현재
게이트를 통과할 수 없습니다. 경로, 자격 증명, VM ID, 게스트 배치 위치,
SDK/도구 집합 및 반복 횟수는 저장소 시스템에 고정된 가정이 아니라 매개변수로
남겨 둡니다.

위의 일반 Verifier 기준선은 과거 증거이며 저자원 시뮬레이션 결과를 뜻하지
않습니다. VM 실행기의 명시적인 `-RequireLowResourcesSimulation` 게이트는 운영자가
준비한 부팅에서 Random 또는 Systematic Low Resources Simulation이 이미 활성화된
경우에만 드라이버를 로드하며 옵션을 직접 켜지 않습니다. 별도의 Random Low
Resources 실행에서 `kernel-browser-https-inspection`의 bounded
workspace/TLS/HTTP 할당 경로를 시험했고, Verifier의 의도적 할당 실패 횟수가
2에서 3으로 증가했습니다. 오류 주입 뒤 배치된 드라이버와 프로세스가 모두
제거되고, 새 충돌 이벤트와 덤프가 없으며, Verifier 설정이 바이트 단위로
동일함을 확인한 뒤에만 안전한 fail-closed 결과로 인정했습니다. 이후 일반
Verifier 구성을 복원하고 specialized-observation을 다시 실행해 이번 실행의
로드/언로드 횟수가 각각 1씩 증가하는 것도 확인했습니다. 임의의 제품군 실패를
통과로 바꾸지는 않습니다.

## 안전 속성

공개 인터페이스는 다음과 같은 잘못된 조합을 의도적으로 표현할 수 없게 만듭니다.

1. 콜아웃이나 필터 키는 다른 레이어에서 사용할 수 없습니다.
2. 일반 분류 콜백은 `FWPS_CLASSIFY_OUT0`를 변경할 수 없습니다.
3. 스트림 콜백은 종료 또는 중재 판정을 반환할 수 없으며, 종료·중재·검사 콜백은
   `stream_result`를 반환할 수 없습니다. 검사 콜백은 `void`를 반환하므로 실수로
   트래픽을 분류할 수 없습니다.
4. 스트림 동작과 바이트 수 필드는 결과 팩터리만 생성합니다. 어댑터는 네이티브
   필터가 UNKNOWN이 아닐 때 아웃바운드 지연, 터미널 버퍼의 추가 데이터 요청,
   검사 편집 및 연결 종료도 거부합니다.
5. 제출한 흐름 컨텍스트는 정확히 한 번 파괴됩니다. 연결에 실패하면 즉시
   파괴하고, 성공하면 등록의 형식화된 flow-delete 트램펄린에서 파괴합니다.
   재설정은 새 연결을 막고 성공한 모든 연결의 종료를 기다린 뒤 콜아웃 등록을
   해제합니다.
6. 일반 패킷 클론과 스트림 데이터 클론은 소유자 유형이 다르며
   따라서 다른 필수 릴리스 기능이 있습니다.
7. 비동기 완료될 때까지 주입 핸들을 삭제할 수 없습니다.
   런다운이 0에 도달합니다.
8. 정책 동작 형식, 엔진 핸들, 네이티브 조건 배열, commit 및 abort는 컨트롤러
   코드에 노출하지 않습니다. 별도의 종료, 검사 및 알 수 없는 동작 빌더가 적법한
   네이티브 동작을 선택합니다.
9. 다양한 트랜잭션의 공급자/하위 계층/콜아웃 기능 또는
   공급자를 결합할 수 없습니다.
10. 콘텐츠 정책은 네이티브 WFP 동작이 아니라 형식화된 판정을 반환합니다. 커널은
    주입/지연 소유권을 유지하고, 한도가 있는 실패 시 차단 시간 초과 및 큐 정책을
    적용합니다. 거부된 TCP 애플리케이션 메시지는 임의의 스트림 바이트를 삭제하는
    대신 흐름 전체를 종료합니다.
11. 연결 리디렉션은 형식화된 ALE connect-redirect 계층에서만 사용할 수 있습니다.
    빌더에는 0이 아닌 프록시 PID와 포트가 필요하고, 커널 작업은 쓰기 가능한
    데이터를 획득할 때마다 정확히 한 번의 apply와 짝을 맞춥니다. 사용자 모드는
    WFP의 불투명 리디렉션 레코드를 해석하거나 만들어낼 수 없습니다.
12. 각 관리 계층은 해당 계층에 유효한 조건만 제공합니다. 중복 필드, 잘못된
    접두사 길이, 지원하지 않는 방향/플래그 값 및 원자적으로 지정하지 않은 ICMP
    형식/코드 쌍은 트랜잭션 전에 실패합니다.
13. 임시 정책은 `install`을 사용합니다. 영구 정책은
    `reconcile` 또는 `uninstall`. 커밋은 정확한 공급자,
    하위 레이어, 설명선 및 필터 키 세트가 선언된 매니페스트와 일치합니다.
14. 네트워크 이벤트 텔레메트리는 네이티브 데이터를 크기가 제한되고 포인터가 없는
    스냅샷으로 복사하며 큐에서 삭제된 이벤트 수도 보고합니다. 시스템 전체 수집
    상태를 변경하려면 명시적 옵션이 필요하며 이전 상태를 복원합니다.

## 환경 의존 런타임 적용 범위

Hyper-V vSwitch 분류와 활성 전송 모드 IPsec 통합 게이트도 위의 대표 듀얼 스택
경로와 함께 런타임으로 검증했습니다. vSwitch 결과에는 기본 제공 WFP 스위치
확장이 활성화된 실제 Hyper-V 스위치 데이터 경로가 필요합니다. IPsec 결과에는
두 피어, 보호된 TCP 및 UDP 트래픽, 양쪽 피어에서 일치하는 양방향 Quick Mode
SA가 필요합니다. 등록 및 컴파일 계약만으로는 여전히 어느 게이트도 충족하지
않습니다. 토폴로지, 합격 기준, 증적 아티팩트 및 정리 범위는
[`test/wfp/runtime/advanced`](runtime/advanced/README.ko-KR.md#hyper-v-vswitch-및-ipsec-증적)에
기록되어 있습니다.

Windows는 내부 고속 계층의 정적 필터링을 지원하지 않으므로 고속 계층은 검사
전용으로 유지합니다. IPsec 정책 계층도 관리 전용이며, 두 범주 모두 콜아웃을
등록할 수 있는 NTL 계층으로 노출하지 않습니다.

WFP 리디렉션 레코드는 TCP 허용 소켓 핸드오프입니다. UDP가 승인되지 않았습니다.
그러한 레코드가 전파될 수 있는 소켓이므로 "TCP가 아닌 리디렉션
레코드 전파"는 누락된 런타임 게이트가 아닙니다. 데이터그램 프록싱 전달
대신에 형식화된 튜플 컨텍스트를 사용하세요.
