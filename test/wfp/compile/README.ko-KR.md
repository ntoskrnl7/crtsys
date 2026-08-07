# ntl::wfp 컴파일 계약

이 픽스처는 공개 WFP 소유권 및 정책 계약을 `/W4 /WX`로 컴파일합니다.

커널 대상은 다음을 다룹니다.

- 공개 `<ntl/net/kernel/all>` 통합 이중 런타임 헤더, 할당 없는
  gRPC/WebSocket/데이터그램 프레이밍, 조각난 정적/Huffman QPACK, 제한된
  ClientHello 관찰, 직접/오프로드 변환 메타데이터, 형식화된 검사 판정, 동기/비동기
  오프로드 경계, QUIC 공급자 경계, drain 가능한 커널 수명
- 강한 형식의 레이어/callout 키와 콜백 범위 밖으로 벗어나지 않는 이벤트
- ALE, flow-established, 패킷, 스트림 콜백 시그니처
- flow-context 소유권 이전, flow-delete 소멸, callout 등록 해제 전 unload drain
- ALE 보류 작업의 소유권
- IPv4/IPv6 ALE 연결 리디렉션 변경, 루프 감지, 쓰기 가능 데이터 마무리
- 복제 패킷의 네트워크 및 전송 계층 주입
- 콜백 범위 패킷/스트림 scatter 뷰 및 제한된 소유 복사본
- 단일 리더 코루틴 스트림 및 WFP 관찰 어댑터
- 역순 callout 정리

사용자 대상은 종료형 ALE 정책, 비종료형 flow 검사, 종료형 stream 편집 및 알 수 없는
동작의 stream 제어를 포함하는 provider/sublayer/callout/filter 그래프를 컴파일합니다.
레이어 범위 조건 빌더는 형식화된 IPv4/IPv6 네트워크, 포트, 프로토콜, ID, 플래그,
VLAN, MAC 주소, 원자적 ICMP 형식/코드 값을 다룹니다. 중복되거나 지원하지 않는
필드는 엔진 트랜잭션을 만들기 전에 거부됩니다. 또한 필수 PID/포트 연결 리디렉션
빌더, 영구 manifest reconciliation, 제한된 네트워크 이벤트 텔레메트리, 이동 전용
사용자 프록시 handoff도 컴파일합니다. 네이티브 엔진 핸들 및 action/condition 배열은
계속 접근할 수 없습니다.

이식 가능한 의미론 대상은 x64와 x86에서 조각난 big-endian 읽기, subview, 복사,
조각을 넘는 쓰기, 조기 종료 열거, 토큰 일치를 실행합니다.

사용자 모드 async-socket 대상은 IOCP 컨텍스트, 이동 전용 소켓, `co_await`
읽기/쓰기 작업을 `/W4 /WX`로 컴파일합니다. stream-edit controller의
`--coroutine-self-test`가 이에 대응하는 loopback 런타임 계약입니다.

사용자 모드 TLS 대상은 x64/x86에서 실제 loopback Schannel 클라이언트/서버 세션을
실행합니다. 핸드셰이크 조각화를 강제하고, 여러 레코드에 걸친 요청/응답 payload를
전송하며, 원래 ClientHello를 파싱하고, 동적으로 SNI leaf를 발급·선택합니다. 이어
애플리케이션 소유 private CA 체인으로 검증하고 사용자 지정 정책 거부, 제한된 TLS
프레이밍 및 HTTP/1 `Content-Length`/`chunked` 계약, TLS 1.3 post-handshake 메시지,
양방향 `close_notify`를 검증합니다.

이식 가능한 프로토콜 대상은 x64/x86에서 WebSocket 조각화와 협상된
`permessage-deflate` context takeover, HTTP/2 frame/continuation 조립, RFC 7541
Huffman 및 dynamic-table HPACK 벡터, 분할된 HTTP/3 stream 및 RFC 9204 static QPACK
벡터, gzip/deflate/Brotli 체인과 그 손상·확장 한계, ALPN 선택, fail-closed TLS 정책
계약을 실행합니다.

같은 이식 가능한 대상은 사용자 모드에서 할당 없는 커널 지향 코어를 교차 검증합니다.
잘못된 오프로드 메타데이터, 판정, ClientHello 압축 방식을 거부하고, 인라인 async
완료·취소·중지·drain을 실행하며, 프로토콜·방향·flow·포트 메타데이터가 명시적인
오프로드 경계를 지나 보존되는지 검증합니다.

HTTP transform 대상은 HTTP/1.1과 HTTP/2 어댑터 전반에서 하나의 의미론적
요청/응답 정책을 실행합니다. chunked 전송 디코딩, 상태 없는 HPACK 출력, 조각난
HEADERS/CONTINUATION, 다중화 요청/응답 상관관계, DATA 재구성, 중첩 gzip/Brotli
재인코딩, validator 제거, fail-closed 및 원본 복원 실패, 차단, 합성 응답을 다룹니다.
또한 제어 문자 주입 및 금지 trailer를 거부하고, 표현의 `Content-Length` 또는
`Content-Encoding`을 잘못 읽지 않으면서 HEAD/304의 본문 없는 framing을 검증합니다.
브라우저 HTTP/3 proxy 계약은 같은 파이프라인을 gzip 응답에 적용하고 재인코딩 결과와
동일한 HEAD 메타데이터 규칙을 확인합니다.

HTTP/2 계약은 제한된 커널 preflight 경계도 실행합니다. 첫 요청이 origin 연결 전에
차단될 수 있는지, 허용된 요청이 이동 전용 workspace와 버퍼링된 frame을 정확히 한 번
전송하는지, 브라우저용 로컬 SETTINGS acknowledgement를 이후 origin 연결로 재생하지
않고 로컬에서 소비하는지를 검증합니다.

transform 계약은 취소, 기한, 큐 과부하를 포함한 고정 풀 코루틴 정책 경로도
실행합니다. streaming 계약은 HTTP/1.1, HTTP/2, HTTP/3의 올바른 헤더 framing으로
같은 chunk 정책을 실행합니다. policy-stress 대상은 큐에 넣은 비동기 메시지 4,096개와
제한된 64 MiB streaming 본문을 완료합니다.

프로토콜 대상은 마스킹한 WebSocket 메시지, 임의 DATA chunk에 나뉜 gzip gRPC 메시지,
WebTransport stream 및 capsule payload도 재작성합니다. 사용할 수 없는 QUIC, ECH,
pinning, 성공한 HTTP/2 interception의 제품 동작을 검증합니다.

브라우저 HTTP/3 대상은 브라우저 예제의 `http3_inspection.*`을 직접 컴파일하고,
x64/x86에서 분할 요청/응답 stream, 별도 QUIC FIN, static QPACK, Brotli HTML 디코딩,
HTML 로깅을 실행합니다.

CTest는 실행 가능한 의미론 계약을 등록합니다. 관리 계약은 결정적으로 생성한 주소로
IPv4 prefix 0~32 전체와 IPv6 prefix 0~128 전체를 검사합니다. 검사 계약은 framing 및
content-search 불변 조건에 결정론적 조각 입력 8,192개를 제공합니다. 결정론적 parser
fuzz 대상은 HTTP/1, HTTP/2+HPACK, HTTP/3+QPACK, WebSocket, gRPC, capsule,
gzip/deflate/Brotli, TLS ClientHello에 걸쳐 생성 입력 16,384개와 seed 절단·변형을
추가합니다. Debug와 Release 모두 x64/x86에서 이 계약을 실행합니다.

`Run-WfpLibFuzzer.ps1`는 선택적인 coverage-guided 게이트입니다. ClangCL, libFuzzer,
AddressSanitizer로 같은 제한된 parser harness를 구성하며, Windows에서 libFuzzer에
필요한 정적 MSVC runtime을 사용합니다. 유효한 HTTP/TLS 입력으로 seed하고
`wfp-fuzz.dict`를 제공하며, 모든 crash artifact를 선택한 build 디렉터리에 보존합니다.
결정론적 실행 파일은 일반 MSVC CTest 계약으로 남습니다. libFuzzer 대상은 일반 WDK
빌드가 Clang 의존성을 갖지 않도록 opt-in입니다.

```powershell
.\Run-WfpLibFuzzer.ps1 -Seconds 1800
```
