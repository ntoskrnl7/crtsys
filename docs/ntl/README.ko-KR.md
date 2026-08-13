# NTL 문서

[README로 돌아가기](../../README.ko-KR.md)

NTL은 `crtsys`에 포함된 선택적 C++ 도우미 계층입니다. 실제 커널 객체 모델을 드러낸 채로, 흔히 쓰는 WDK 드라이버 제어 패턴을 RAII 친화적인 C++ 형식으로 래핑합니다.

간결한 [NTL API 참조](../ntl-api.ko-KR.md)보다 자세한 설명이 필요할 때 이 문서를 사용하십시오.

재사용 가능한 네트워크 프로토콜 및 전송 헤더는 `<ntl/net/...>` include 접두사와 `ntl::net` namespace를 사용합니다. 프로토콜별 API는 `ntl::net::http`, `ntl::net::http2`, `ntl::net::http3`와 같은 하위 namespace를 사용합니다. Windows 전용 통합 adapter는 `ntl::wfp`처럼 독립적인 최상위 include 접두사와 namespace를 유지합니다.

빌드 가능한 예제:

- [NTL 타입이 지정된 IOCTL 예제 드라이버](../../examples/ntl-driver)
- [NTL RPC 예제 드라이버](../../examples/ntl-rpc-driver)
- [NTL 커널/사용자 모드 네트워킹 계약 테스트](../../test/net/kernel-contracts)
- [NTL KMDF 드라이버/앱 예제](../../examples/kmdf)
- [NTL KMDF DMA 드라이버 템플릿](../../examples/kmdf/dma)
- [NTL KMDF USB 드라이버 템플릿](../../examples/kmdf/usb)
- [NTL KMDF WMI 드라이버/앱 예제](../../examples/kmdf/wmi)
- [NTL KMDF 버스/PDO 드라이버/앱 예제](../../examples/kmdf/bus)
- [NTL 미니필터 드라이버/앱 예제](../../examples/minifilter)
- [NTL WFP ALE connect-block 드라이버/controller](../../examples/wfp/kernel/ale-connect-block)
- [NTL WFP connect-redirect 코루틴 TCP 프록시](../../examples/wfp/user/connect-redirect)
- [NTL WFP TLS 평문 검사 프록시](../../examples/wfp/user/tls-inspection-proxy)
- [NTL WFP 브라우저 HTTPS 검사](../../examples/wfp/user/browser-https-inspection)
- [NTL WFP 사용자/커널 모드 쌍 예제](../../examples/wfp)
- [NTL 커널 직접 TCP 콘텐츠 필터](../../examples/wfp/kernel/tcp-content-filter)
- [NTL 커널 직접 UDP 콘텐츠 필터](../../examples/wfp/kernel/udp-content-filter)
- [NTL 커널 직접 connect redirect](../../examples/wfp/kernel/connect-redirect)
- [NTL 커널 직접 TLS 검사 프록시](../../examples/wfp/kernel/tls-inspection-proxy)
- [NTL 커널 직접 브라우저 HTTPS 검사](../../examples/wfp/kernel/browser-https-inspection)
- [NTL 커널 직접 HTTP/3 검사](../../examples/wfp/kernel/http3-inspection)

## 주제

| 주제 | 용도 |
| --- | --- |
| [실행 문맥과 IRQL](./context.ko-KR.md) | 공통 IRQL 용어와 NTL 실행 문맥 설명을 해석하는 방법 |
| [상태, 예외, 스택 확장](./status-exceptions-stack.ko-KR.md) | `ntl::status`, `ntl::exception`, SEH 경계 도우미, `ntl::expand_stack` |
| [Result](./result.ko-KR.md) | 드라이버 제어 경로를 위한 `NTSTATUS` 기반 값 또는 오류 도우미 |
| [핸들 및 객체 소유권](./ownership.ko-KR.md) | Win32 `CloseHandle`, 커널 `ZwClose`, `ObDereferenceObject` 소유권 |
| [파일 객체 뷰](./file-object.ko-KR.md) | 비소유 `PFILE_OBJECT` 및 `WDFFILEOBJECT` 뷰와 소유권 경계 |
| [레지스트리](./registry.ko-KR.md) | Zw 레지스트리 키 RAII 래퍼, 드라이버 `Parameters` 조회, 타입이 지정된 값 쿼리/설정 도우미 |
| [드라이버, 장치, IRP 도우미](./driver-device-irp.ko-KR.md) | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::device_endpoint`, dispatch 콜백, `ntl::irp` |
| [KMDF 도우미](./kmdf.ko-KR.md) | 선택적 `ntl::kmdf::main`, C++ context, 타입이 지정된 I/O, 수동 queue와 취소, 하드웨어 리소스, 전원 정책, DMA/USB/WMI, 공통 WDF 객체, interrupt/timer/work item, child list/PDO, 타입이 지정된 query interface, 레지스트리, 장치 속성 래퍼 |
| [KMDF 엔지니어링 점검표](./kmdf-driver-checklist.ko-KR.md) | 소유권, 콜백 수명, 요청 취소, PnP/전원, ABI, Driver Verifier, 소프트웨어/하드웨어 릴리스 기준 |
| [미니필터 도우미](./minifilter.ko-KR.md) | `ntl::flt::main`, 타입이 지정된 pre/post 콜백과 context, 볼륨별 metadata, 소유 레거시 제어 장치, 타입이 지정된 통신 포트와 공유 영역 |
| [드라이버 개발자를 위한 WFP 가이드](./wfp-guide.ko-KR.md) | WFP 개념, 커널 중심/사용자 모드 중심 실행 모델, 타입이 지정된 callout 결정, payload 경계, TLS/QUIC 검사, 예제 순서, 검증 |
| [WFP 도우미](./wfp.ko-KR.md) | 타입이 지정된 callout layer와 condition, 안전한 connect redirect와 proxy handoff, 조각난 packet/stream view, 제한된 coroutine 관찰, flow context, injection 소유권, 명시적 session/지속 정책 수명, 상태 점검, event telemetry |
| [네트워크 이중 런타임 모델](./network-dual-runtime.ko-KR.md) | 사용자 및 커널 코드 전반의 제한된 하나의 protocol/policy 계약, 명시적 direct/offload capability, drain 중인 커널 실행, QUIC provider 경계 |
| [콘텐츠 검사와 framing](./inspection.ko-KR.md) | 완전한 UDP/TCP 메시지 경계, 구조화된 판정, 사용자 정의 framer, decoder adapter, 제한된 HTTP/3 검사 구성, TLS 평문 경계 |
| [HTTP, WebSocket, gRPC, WebTransport 검사](./protocol-inspection.ko-KR.md) | 공통 sync/async/stream transform, HTTP/1, HTTP/2/HPACK, HTTP/3/QPACK, WebSocket, gRPC, WebTransport, content coding, ALPN, ECH, pinning, mTLS, 제품 적용 경계 |
| [사용자 모드 Schannel TLS stream](./tls-stream.ko-KR.md) | coroutine Schannel I/O, 제한된 ClientHello/SNI 관찰, 주입 가능한 host별 certificate 발급/cache, TLS 평문 framing, HTTP/1 경계, 정상 종료 |
| [장치 제어 패턴](./device-control-pattern.ko-KR.md) | 타입이 지정된 IOCTL, remove lock, MDL, 출력 보고를 사용하는 실용적 `IOCTL` dispatch 패턴 |
| [타입이 지정된 IOCTL 도우미](./ioctl.ko-KR.md) | 요청/응답 payload 형식과 연결된 컴파일 타임 `CTL_CODE` descriptor |
| [장치 인터페이스](./device-interface.ko-KR.md) | PnP `IoRegisterDeviceInterface` 소유권과 enable/disable 도우미 |
| [RPC](./rpc.ko-KR.md) | 커널/사용자 모드 RPC schema, 안정적인 callback ID, framing 검사, x86/x64 wire 규칙 |
| [IPC 공유 메모리](./ipc.ko-KR.md) | IOCTL RPC 및 미니필터 통신 포트를 위한 전송 중립 영역 token과 제한된 공유 메모리 ring |
| [동기화](./synchronization.ko-KR.md) | `ntl::irql`, IRQL query/contract 도우미, spin lock, ERESOURCE 래퍼, lock 도우미 |
| [Remove lock](./remove-lock.ko-KR.md) | dispatch/remove/unload 동기화를 위한 `IO_REMOVE_LOCK` RAII guard |
| [Event](./event.ko-KR.md) | 알림/동기화 event용 `KEVENT` 래퍼 |
| [Timer 및 DPC](./timer.ko-KR.md) | one-shot/periodic timer와 DPC queueing을 위한 `KTIMER`, `KDPC` 래퍼 |
| [시스템 스레드](./system-thread.ko-KR.md) | `NTSTATUS` result와 `ZwClose` handle 소유권을 갖는 `PsCreateSystemThread` 도우미 |
| [대기 도우미](./wait.ko-KR.md) | event, timer, system-thread 래퍼용 공통 timeout 및 wait-status 도우미 |
| [Work item](./work-item.ko-KR.md) | 상주 작업을 `PASSIVE_LEVEL` 시스템 worker thread로 연기 |
| [Passive executor](./passive-executor.ko-KR.md) | callable을 `PASSIVE_LEVEL`에서 실행하는 inline-or-defer 정책 |
| [커널 코루틴 실행 문맥](./coroutine.ko-KR.md) | 명시적으로 연기된 continuation을 `PASSIVE_LEVEL`에서 재개하는 선택적 C++20 awaiter |
| [사용자 모드 coroutine socket](./async-socket.ko-KR.md) | cancellation 및 명시적 task/buffer 수명을 갖는 IOCP 기반 `read_some_borrowed`, `read_exactly_borrowed`, 소유형 `write_all` |
| [Pool allocator](./pool-allocator.ko-KR.md) | 커널 pool 기반 소유권 도우미, STL allocator, PMR resource, pool tag, IRQL 규칙 |
| [Lookaside list](./lookaside-list.ko-KR.md) | `LOOKASIDE_LIST_EX` 기반 고정 크기 커널 객체 cache 래퍼 |
| [MDL 도우미](./mdl.ko-KR.md) | `IoAllocateMdl`이 할당한 MDL의 RAII 소유권 |
| [I/O buffer 매핑과 미니필터 교체](./io-buffer-mapping.ko-KR.md) | IRP/미니필터 input-output 매핑과 연산 중립적인 교체 buffer |
| [심볼릭 링크](./symbolic-link.ko-KR.md) | `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` RAII 래퍼 |
| [유니코드 문자열](./unicode-string.ko-KR.md) | `std::wstring` 저장소를 `UNICODE_STRING`에 맞게 조정 |

## 실행 문맥 규칙

NTL API는 주로 드라이버 초기화, unload, 장치 제어 등 제어 경로를 위해 설계되었습니다. 문서에 더 넓은 계약이 명시되지 않았다면 `PASSIVE_LEVEL`로 가정하십시오.

NTL이 하위 수준 WDK primitive를 노출하는 경우에도 primitive 자체의 네이티브 IRQL 계약은 중요합니다. 예를 들어 원시 nonpaged pool 할당은 WDK pool 할당 규칙을 따를 수 있지만, 그 할당자를 사용하는 STL container에는 생성자, 소멸자, 비교, 예외 등 런타임 동작도 포함됩니다. 정확한 연산과 요소 형식을 별도로 검토하지 않았다면 container 사용은 `PASSIVE_LEVEL`로 취급하십시오.
