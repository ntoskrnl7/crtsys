# NTL API 참조

[README로 돌아가기](../README.ko-KR.md)

NTL은 `crtsys`와 함께 제공되는 작은 C++ 도우미 계층입니다. 기본 WDK 객체를
감추지 않으면서 RAII 방식의 소유권 관리, 콜백 등록, 풀 할당 도우미, 사용자 모드와
커널 모드 사이의 간단한 RPC를 사용하려는 커널 드라이버 코드를 위한 것입니다.

헤더는 [`include/ntl`](../include/ntl)에 있습니다. 전송 방식에 종속되지 않는 네트워킹,
프레이밍, TLS, HTTP, 검사 헤더는 [`include/ntl/net`](../include/ntl/net) 아래에
모여 있으며 `ntl::net` 네임스페이스를 사용합니다. 프로토콜별 하위 네임스페이스로는
`ntl::net::http`, `ntl::net::http3` 등이 있습니다. `ntl::wfp`처럼 Windows에
특화된 통합 계층은 별도의 최상위 디렉터리와 네임스페이스에 유지됩니다.

상세 API 참조는 주제별로 나뉘어 있습니다.

| 주제 | 내용 |
| --- | --- |
| [컨텍스트와 IRQL](./ntl/context.ko-KR.md) | 공통 IRQL 용어와 실행 컨텍스트 규칙 |
| [상태, 예외, 스택 확장](./ntl/status-exceptions-stack.ko-KR.md) | `ntl::status`, `ntl::exception`, SEH 경계 도우미, `ntl::expand_stack` |
| [Result](./ntl/result.ko-KR.md) | `ntl::result<T>`, `ntl::result<void>`, `ntl::unexpected`, `ntl::ok`, result를 반환하는 팩터리 도우미 |
| [핸들과 객체 소유권](./ntl/ownership.ko-KR.md) | `ntl::unique_handle`, `ntl::unique_object`, `try_reference_object_by_handle` |
| [파일 객체 래퍼](./ntl/file-object.ko-KR.md) | `ntl::file`, `ntl::kmdf::file`, WDM/KMDF 파일 객체의 소유권 경계 |
| [레지스트리](./ntl/registry.ko-KR.md) | `ntl::registry_key`, `ntl::registry_value`, `ntl::driver_config`, `try_open_driver_parameters`, 타입이 지정된 레지스트리 값 조회/설정 도우미 |
| [드라이버, 장치, IRP 도우미](./ntl/driver-device-irp.ko-KR.md) | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::device_endpoint`, `try_create_device`, 디스패치 콜백, `ntl::irp` |
| [KMDF 도우미](./ntl/kmdf.ko-KR.md) | `ntl::kmdf::main`, C++ WDF 컨텍스트, 타입 안전한 수동 큐 소유권 및 취소, 하드웨어 리소스와 전원 정책, DMA/USB/WMI, 공통 WDF 객체, I/O/인터럽트/타이머/작업 항목/자식 목록/PDO 래퍼, 타입이 지정된 드라이버 정의 query interface, 레지스트리/속성 도우미 |
| [KMDF 엔지니어링 점검표](./ntl/kmdf-driver-checklist.ko-KR.md) | 운영 환경 드라이버의 소유권, 콜백, 취소, PnP/전원, ABI, 검증 규칙을 검토하기 위한 점검표 |
| [미니필터 도우미](./ntl/minifilter.ko-KR.md) | `ntl::flt::main`, 타입이 지정된 Filter Manager 작업 콜백, RAII 이름 정보, 타입이 지정된 파일/스트림/스트림 핸들 컨텍스트 |
| [WFP 도우미](./ntl/wfp.ko-KR.md) | 계층별로 타입이 지정된 callout, 안전한 연결 리디렉션과 프록시 인계, 조각난 바이트 뷰, 용량이 제한된 코루틴 관찰, 규칙 안전 결과, flow 컨텍스트와 주입 소유권, 트랜잭션 기반 사용자 모드 정책 |
| [네트워크 이중 런타임 모델](./ntl/network-dual-runtime.ko-KR.md) | 명시적인 직접/오프로딩 백엔드를 갖춘 커널 안전 프레이밍, ClientHello, HTTP/2/3, QPACK, gRPC, WebSocket, WebTransport, 변환 계약 |
| [콘텐츠 검사와 프레이밍](./ntl/inspection.ko-KR.md) | UDP 데이터그램과 TCP 메시지 경계, 타입이 지정된 정책 판정, 사용자 정의 프레이머, 디코더 어댑터, 용량이 제한된 HTTP/3 검사 구성, TLS 한계 |
| [사용자 모드 Schannel TLS 스트림](./ntl/tls-stream.ko-KR.md) | Schannel 코루틴 I/O, ClientHello/SNI 인계, 주입 가능한 호스트별 발급 및 신원 캐시, 사설 CA 검증, TLS 평문 프레이밍, 용량이 제한된 HTTP/1 경계, `close_notify` |
| [타입이 지정된 IOCTL 도우미](./ntl/ioctl.ko-KR.md) | `ntl::ioctl`, `is_ioctl`, 타입이 지정된 입력/출력 버퍼 도우미 |
| [장치 인터페이스](./ntl/device-interface.ko-KR.md) | `ntl::device_interface_link`, `try_register_device_interface` |
| [RPC](./ntl/rpc.ko-KR.md) | RPC 스키마 매크로, 서버 수명, 사용자 모드 클라이언트 호출 |
| [동기화](./ntl/synchronization.ko-KR.md) | `ntl::irql`, IRQL 계약 도우미, 스핀 락, ERESOURCE 래퍼 |
| [제거 잠금](./ntl/remove-lock.ko-KR.md) | `ntl::remove_lock`, `ntl::remove_lock_guard` |
| [이벤트](./ntl/event.ko-KR.md) | 알림/동기화 이벤트용 `KEVENT` 래퍼 |
| [타이머와 DPC](./ntl/timer.ko-KR.md) | `ntl::timer`, `ntl::kdpc`, 일회성 타이머, 주기 타이머, 직접 DPC 큐잉 |
| [시스템 스레드](./ntl/system-thread.ko-KR.md) | `ntl::system_thread`, `PsCreateSystemThread`, `join`, 네이티브 스레드 핸들 소유권 |
| [대기 도우미](./ntl/wait.ko-KR.md) | `ntl::zero_timeout`, `ntl::relative_timeout_ms`, `ntl::try_wait`, `ntl::wait_for` |
| [작업 항목](./ntl/work-item.ko-KR.md) | 상주 작업을 `PASSIVE_LEVEL` 시스템 작업자 스레드로 지연 |
| [Passive executor](./ntl/passive-executor.ko-KR.md) | `ntl::passive_executor`, 인라인 PASSIVE 실행, 분리된 nonpaged 작업 게시 |
| [커널 코루틴 컨텍스트](./ntl/coroutine.ko-KR.md) | C++20 `ntl::resume_on_passive`, 큐 실패 상태, continuation 수명 |
| [사용자 모드 코루틴 소켓](./ntl/async-socket.ko-KR.md) | C++20 IOCP 소켓 awaiter, 부분 전송 루프, 취소, 컨텍스트/작업 수명 |
| [풀 할당자](./ntl/pool-allocator.ko-KR.md) | 커널 풀 기반 소유권 도우미, STL 할당자, PMR 리소스, 풀 태그, IRQL 규칙 |
| [Lookaside 목록](./ntl/lookaside-list.ko-KR.md) | `LOOKASIDE_LIST_EX`를 감싼 고정 크기 커널 객체 캐시 래퍼 |
| [MDL 도우미](./ntl/mdl.ko-KR.md) | `ntl::mdl` 소유권과 매핑 도우미 |
| [심볼릭 링크](./ntl/symbolic-link.ko-KR.md) | `IoCreateSymbolicLink` / `IoDeleteSymbolicLink`의 RAII 래퍼 |
| [유니코드 문자열](./ntl/unicode-string.ko-KR.md) | `std::wstring` 저장소를 `UNICODE_STRING`에 맞게 변환 |

드라이버와 앱을 처음부터 끝까지 잇는 코드 조각은 [NTL 사용 예제](./usage-examples.ko-KR.md)를 참고하세요.
