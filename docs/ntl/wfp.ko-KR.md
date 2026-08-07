# Windows 필터링 플랫폼 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::wfp`는 WFP 콜아웃과 정책 관리 수명 주기를 형식화한 경계입니다. WFP 레이어를
숨기거나 패킷 처리를 범용 방화벽 DSL로 바꾸려는 API가 아닙니다. 네이티브 API에서
심각한 정확성 버그를 일으키는 조합을 일반 드라이버 코드가 사용할 수 없게 만드는
것이 목적입니다.

커널 API와 컨트롤러 API는 의도적으로 분리되어 있습니다.

- `<ntl/wfp/callout>`, `<ntl/wfp/classify>`, `<ntl/wfp/flow>`,
  `<ntl/wfp/packet>`, `<ntl/wfp/stream>`, `<ntl/wfp/injection>`은 커널 전용입니다.
- `<ntl/wfp/connect_redirect>`는 현재 빌드 모드에 따라 커널 리디렉션 핸들 API 또는
  사용자 모드의 accept된 소켓 전달 API를 선택합니다.
- `<ntl/wfp/management>`는 사용자 모드 전용입니다.
- `<ntl/wfp/all>`는 올바른 쪽을 선택합니다.

빌드 통합은 Windows 8 이상을 대상으로 하며 다음과 같은 경우 `fwpkclnt.lib`를 연결합니다.
WFP가 선택되었습니다. Visual Studio/NuGet 소비자는 **NTL WFP**를 선택합니다.
**crtsys WDM 진입점** 속성 페이지 또는 해당 MSBuild 설정
속성:

```xml
<CrtSysWdmEntryPoint>NtlWfp</CrtSysWdmEntryPoint>
```

CMake 소비자는 다음을 사용합니다.

```cmake
crtsys_add_driver(my_callout WFP NTL src/main.cpp)
```

예제는 한 디렉터리에 두 설계를 섞지 않고 실행 경계에 따라 나눕니다.

- [`examples/wfp/user`](../../examples/wfp/user/)는 프로토콜 파싱, 인증서 정책,
  콘텐츠 판정을 컨트롤러나 프록시에 둡니다.
- [`examples/wfp/kernel`](../../examples/wfp/kernel/)에는 직접 커널 구현과 네이티브
  WFP 기본 기능 예제가 있습니다.

TCP/UDP 콘텐츠 필터링, 연결 리디렉션, TLS 검사, 제어된 브라우저 HTTPS 캡처,
HTTP/3에는 서로 대응하는 사용자/커널 예제가 있습니다. 커널 버전은 별도
프로젝트이며 사용자 모드 예제를 조용히 다른 설계로 바꾸지 않습니다. 흐름 관찰과
스트림 편집 같은 기본 콜아웃 예제는 빈 사용자 모드 복사본을 만들어도 새 계약을
설명하지 못하므로 커널 전용으로 둡니다.

NuGet `NTL WFP` 프로젝트는 패키지의 감사된 커널을 자동으로 연결합니다.
zlib/Brotli 아카이브. 소스 기반 CMake 프로젝트는 필요할 때만 옵트인합니다.

```cmake
crtsys_add_driver(
  my_callout WFP NTL KERNEL_CONTENT_CODECS src/main.cpp)
```

커널 드라이버는 고정된 MsQuic ABI, Windows 10 버전-2004 대상을 선택합니다.
NMR 클라이언트를 하나의 드라이버 옵션으로 가져옵니다.

```cmake
crtsys_add_driver(
  my_h3_callout
  WFP NTL KERNEL_MSQUIC KERNEL_CONTENT_CODECS
  src/main.cpp)
```

`KERNEL_MSQUIC`는 공급자가 아닌 헤더와 `netio.lib`를 제공합니다. 사용자 모드
고정된 ABI만 필요한 CMake 대상은 계속 호출할 수 있습니다.
`crtsys_add_ntl_msquic_headers()` 및 `crtsys_ntl_msquic_headers`를 연결합니다.
NuGet 및 사전 빌드된 오프라인 번들은 동일한 SHA-256 인증 헤더를 전달합니다.
포함 디렉터리를 자동으로 추가합니다. 사용자 애플리케이션은
호환 `msquic.dll`; 커널 NuGet 소비자는 다음을 선택합니다.
`<CrtSysUseNtlKernelMsQuic>true</CrtSysUseNtlKernelMsQuic>`는 다음을 선택합니다.
Windows 10 버전 2004 이상 계약 및 `netio.lib`, 바인딩
호환 가능하고 명시적으로 설치된 MsQuic NMR 공급자.
건물은 둘 중 하나를 설치하거나 시작하지 않습니다.

## API에 의해 시행되는 규칙

### 레이어와 키는 원시 GUID가 되지 않습니다.

제공자, 하위 레이어, 콜아웃 및 필터 키는 고유한 유형입니다. 콜아웃 및
필터 키에는 레이어 유형도 포함되어 있습니다.

```cpp
using layer = ntl::wfp::layers::ale_auth_connect_v4;

constexpr GUID native_callout = /* project-owned stable GUID */;
constexpr ntl::wfp::arbitrating_callout_key<layer> callout(native_callout);
```

`stream_v4`의 키를 `ale_auth_connect_v4`에 전달할 수 없습니다.
등록 또는 필터. 기본 GUID 액세스는 비공개이며 다음 사용자만 사용할 수 있습니다.
등록 및 정책 작성자.

### 분류 콜백은 필터에서 허용하는 결정을 반환합니다.

일반 분류 콜백은 복사할 수 없는 콜백 범위를 수신합니다.
읽기 전용 `classify_event<Layer>`. 콜아웃 키와 콜백 결과가 인코딩됩니다.
동일한 기본 WFP 작업 계약:

```cpp
constexpr auto authorize =
    +[](const ntl::wfp::classify_event<layer>& event) noexcept {
      const auto port =
          event.value(layer::field::remote_port).uint16();
      return port && *port == 443
                 ? ntl::wfp::arbitration_decision::block
                 : ntl::wfp::arbitration_decision::continue_classification;
    };
```

콜백은 `FWPS_CLASSIFY_OUT0`에 액세스할 수 없습니다. 트램펄린이 적용됩니다`FWPS_RIGHT_ACTION_WRITE`, 명확한 조치 권한, 거부권 및 의미론 흡수.
컨트롤러와 드라이버는 동일한 유형의 키를 사용해야 합니다.

- `inspection_callout_key` / `add_inspection()`: `void` 관찰자 콜백;
  WFP 분류는 자동으로 계속됩니다.
- `terminating_callout_key` / `terminating_decision`: 허용 또는 차단; 그리고
- `arbitrating_callout_key` / `arbitration_decision`: 계속, 허용 또는
  블록(`FWP_ACTION_CALLOUT_UNKNOWN`).

`stream_callout_key`는 `stream_result` 및 고정 스트림과 쌍을 이룹니다.
알 수 없는 작업입니다. 관찰 전용 스트림 콜백은 대신 다음을 사용합니다.
`inspection_callout_key` 및 `add_stream_inspection()`; `void`를 반환합니다.
콜백은 `noexcept`입니다. 시행 콜백은 정확히 다음을 반환해야 합니다.
키에 의해 선택된 결정 유형, 검사 콜백은 정확하게 반환됨
`void`. 일치하지 않는 컨트롤러 필터, 드라이버 등록 또는 콜백
반환 유형이 컴파일되지 않습니다.

지원되는 결정 레이어는 다음과 같습니다.

- `ale_connect_redirect_v4` 및 `ale_connect_redirect_v6`;
- `ale_auth_connect_v4` 및 `ale_auth_connect_v6`;
- `ale_auth_recv_accept_v4` 및 `ale_auth_recv_accept_v6`;
- `ale_flow_established_v4` 및 `ale_flow_established_v6`;
- `datagram_data_v4` 및 `datagram_data_v6`; 그리고
- 인바운드/아웃바운드 전송 v4 및 v6; 그리고
- `outbound_ip_packet_v4` 및 `outbound_ip_packet_v6`.

### 연결 리디렉션에는 하나의 합법적인 변형 경로가 있습니다.

`ALE_CONNECT_REDIRECT`에는 쓰기 가능한 레이어 데이터가 필요하지만 일반 콜백은 여전히
`FWPS_CONNECT_REQUEST0` 또는 `FWPS_CLASSIFY_OUT0`에 액세스할 수 없습니다. 드라이버가 생성
하나의 캐시된 리디렉션 핸들을 사용하고 전체 작업을 위임합니다.

```cpp
auto redirector =
    ntl::wfp::connect_redirector::try_create(provider_key);

return redirector->redirect(
    event, ntl::wfp::local_proxy_target{proxy_pid, proxy_port});
```

작업은 동일한 핸들에 의해 이미 리디렉션된 연결을 감지합니다.
각각의 성공적인 쓰기 가능한 데이터 수집을 하나로 연결합니다.
`FwpsApplyModifiedLayerData0`는 원래 엔드포인트를 WFP 소유로 전송합니다.
컨텍스트 및 실패 시 차단됩니다. 정책측에서는 다음을 사용해야 합니다.
`connect_redirect_filter_builder`; 생성자에는 프록시 PID가 필요하며
호스트 순서 포트를 수정하고 종료 작업 및 원시 컨텍스트 인코딩을 수정합니다.

수락하는 프록시는 일치하는 핸드오프를 캡처하고 아웃바운드 구간을 엽니다.

```cpp
auto handoff =
    ntl::wfp::redirected_connection::capture(accepted_socket);
SOCKET outbound = handoff.connect_original();
```

`connect_original()`는 연결하기 전에 WFP의 불투명 리디렉션 레코드를 첨부합니다.
원래 목적지로. 이렇게 하면 연결 속성이 보존되고
커널 루프 검사는 프록시의 아웃바운드 연결을 허용합니다. API는
TCP 바이트 스트림 프록시 경계; TLS를 해독하거나 정의하지 않습니다.
응용 프로그램 메시지 코덱. 사용자 모드 프록시는 다음과 같이 핸드오프를 구성할 수 있습니다.
`<ntl/net/tls/stream>`는 허용된 레그에서 TLS를 종료하고
outbound leg를 구성합니다. WFP callout에는 여전히 TLS 평문이나 key가 보이지 않습니다.

### 스트림 콜백의 결과 유형이 다릅니다

스트림 분류에는 매핑되지 않는 합법적인 출력 조합이 있습니다.
일반적인 허가/차단 결정. 따라서 `add_stream()`는
`stream_result`를 반환하는 콜백:

```cpp
struct flow_state {
  std::uint64_t inspected = 0;
  ~flow_state() noexcept = default;
};

constexpr auto inspect =
    +[](const ntl::wfp::stream_event<
           ntl::wfp::layers::stream_v4, flow_state>& event) noexcept {
      if (!event.context())
        return ntl::wfp::stream_result::block(event.data().size());

      event.context()->inspected += event.data().size();
      return ntl::wfp::stream_result::permit(event.data().size());
    };
```

공장은 `permit(bytes)`, `block(bytes)`, `need_more(minimum)`,
`defer()`, `drop_connection()` 및 `allow_connection()`. 어댑터가 바인딩됩니다.
분류 작업, 스트림 작업, 필수/시행 개수 및 작업 권한
확인하세요. 적용된 바이트 수는 표시된 스트림 길이로 고정됩니다.
검사 필터가 편집자가 되는 것을 방지하고 반복적으로 변환합니다.
WFP의 버퍼 제한에서 더 많은 데이터가 필요하거나 더 이상 데이터가 없는 상태에서 페일클로즈됩니다.
전체 버퍼 블록, 아웃바운드 지연을 거부하고 연결 끊김을 방출합니다.
알 수 없는 작업 기본 필터.

`stream_data_view::bytes()`는 native NBL/MDL 조각을 보는, 할당 없는
`scatter_view`입니다. `copy_to()`는 편리한 평탄화 경로로 유지됩니다.
`cloned_stream_data`는 이동 전용 out-of-band 경로이며 항상
`FwpsDiscardClonedStreamData0`을 사용하므로 일반 NBL clone과 혼동할 수 없습니다.

### 흐름 컨텍스트는 소유권을 한 번 이전합니다.

`callout_driver::add_stream<Context>()` 및
`add_flow_context<Context>()`는 다음을 반환합니다.
`flow_target<Layer, Context>`. 대상은 동일한 레이어와 컨텍스트만 허용합니다.
유형:

```cpp
auto state = std::make_unique<flow_state>();
const ntl::status status =
    stream_target.associate(flow_handle, std::move(state));
```

성공하면 소유권은 내부 등록 보유자 및 WFP에게 이전됩니다.
해당 보유자의 불투명한 신원만 수신합니다. 등록은 모든 것을 추적합니다.
보유자; flow-delete 트램폴린은 컨텍스트를 정확히 한 번 삭제합니다. 켜기
실패하면 실패한 전송 경로에 의해 컨텍스트가 삭제됩니다. 컨텍스트 저장
소멸자는 대상 레이어가 삭제되는 IRQL에서 안전해야 합니다.
흐름; 소멸자는 `noexcept`여야 합니다.

데이터그램 흐름 컨텍스트는 다음 형식의 콜백을 사용합니다.

```cpp
decision callback(const classify_event<datagram_data_v4>&,
                  proxy_context*) noexcept;
```

원시 정수 흐름 컨텍스트는 애플리케이션 코드에 의해 캐스팅되지 않습니다.

### 지연된 패킷에는 명시적인 소유권이 있습니다.

- `borrowed_packet`는 콜백에만 유효합니다.
- `referenced_packet`는 WFP NBL 참조를 보유합니다.
- `cloned_packet`는 다음에 의해 생성된 NBL을 소유하고 있습니다.
  `FwpsAllocateCloneNetBufferList0`.
- `cloned_stream_data`는 `FwpsCloneStreamData0`가 생성한 체인을 소유하고 있습니다.
- `pended_operation`는 소멸자에서 ALE 작업을 완료합니다.
  이미 완료되었습니다.

`network_injector`, `transport_injector` 및 `stream_injector` 자체 공유
주입 상태. 성공적인 비동기 주입은 패킷 소유자를 전송합니다.
완료 컨텍스트로. `injection_limits::maximum_in_flight`가 시행됩니다.
제출 전; 피로는 없이 `STATUS_QUOTA_EXCEEDED`를 반환합니다.
호출자의 패킷을 소비합니다. `close()`/destruction은 새로운 작업을 거부하고,
네이티브 핸들은 허용된 모든 완료가 소모될 때까지 활성 상태로 유지됩니다. 마지막
APC 또는 DISPATCH의 릴리스는 런타임 PASSIVE 정리 도메인으로 폐기됩니다.
따라서 호출자는 정리 작업을 예약하거나 파괴 IRQL을 기억하지 않습니다.

아웃바운드 전송 주입도 이동 전용을 사용합니다.
`transport_send_request`. 원격 주소를 딥 복사하거나 채택하고
엔드포인트, 주소 계열, 범위 및
구획. `transport_injector::inject_send()`는 클론과 해당 클론을 모두 이동합니다.
동일한 완료 컨텍스트로 요청합니다. 이는 WFP가
비동기화될 때까지 `FWPS_TRANSPORT_SEND_PARAMS0`가 참조하는 버퍼
완성; 따라서 스택 지원 기본 매개변수 블록은 다음에 의해 노출되지 않습니다.
NTL API.

```cpp
auto request = ntl::wfp::transport_send_request::try_copy(
    endpoint, AF_INET6, compartment, remote_address, remote_scope,
    control_data, ntl::net::buffer_limits{4096});
if (!request)
  return ntl::wfp::terminating_decision::block_and_absorb;

const auto injected = injector.inject_send(
    std::move(clone), std::move(*request));
```

동기 제출 실패는 완료 컨텍스트를 즉시 삭제합니다.
성공적으로 제출하면 WFP 완료 콜백에서 해당 항목이 삭제됩니다. 인젝터
재설정하면 새로운 제출이 중지됩니다. 런다운 및 수동 정리는 허용된 패킷을 유지합니다.
콜백을 통해 소유자와 기본 상태가 살아있습니다. 그 길은 하나도 없어
호출자 관리 버퍼 수명 또는 두 번째 정리 분기가 필요합니다.

`stream_event`는 형식화된 `stream_injection_site<Layer>`를 실행할 수 있습니다. 그것은 캡처
노출하지 않고 WFP에서 제공하는 흐름, 콜아웃, 레이어 및 스트림 플래그
해당 ID에 대한 생성자. 이 사이트는 또한
`continue_deferred()`.

## 수동 MDL 탐색 없이 단편화된 바이트

`<ntl/net/buffer/scatter_view>`는 공통의 기본 중립 바이트 계층입니다. 그것은 의도적으로
WFP 또는 NDIS 콜백 유형이 포함되어 있지 않습니다.

- `scatter_view`는 읽기 전용 조각을 빌리고 수명을 연장하지 않습니다.
- `mutable_scatter_view`는 소유하거나 명시적으로 변경 가능한 개체에 의해서만 반환됩니다.
  어댑터;
- `borrowed_byte_cursor`는 제한된 순차 및 빅엔디안 읽기를 수행합니다.
- `scan_bytes()`는 조각 경계를 넘어 고정 토큰을 찾습니다.
  평탄화; 그리고
- `owned_bytes`는 필수 항목이 포함된 이동 전용 비페이징 딥 카피입니다.
  `buffer_limits` 확인.

패킷 래퍼는 차이점을 직접적으로 노출합니다.

```cpp
const auto borrowed = event.packet();
ntl::net::borrowed_byte_cursor header(borrowed.bytes()); // callback lifetime only

auto saved = borrowed.try_copy(ntl::net::buffer_limits{64 * 1024});
if (!saved)
  return ntl::wfp::terminating_decision::block_and_absorb;
```

`borrowed_packet::bytes()` 및 `stream_data_view::bytes()`는 모든 항목을 열거합니다.
`NET_BUFFER_LIST`, `NET_BUFFER` 및 MDL 조각. UDP 포트 필드가 교차할 수 있음
MDL 경계; `cloned_packet::rewrite_udp_destination_port()`는 여전히 편집 중입니다.
이를 확인하고 체크섬을 지웁니다. `NdisGetDataBuffer()` 연속성 가정은 없습니다.
필수.

뷰의 공급자와 백업 저장소는 전체 기간 동안 유효해야 합니다.
운영. 분류 이벤트에서 얻은 뷰는 이벤트 이후에 유지되어서는 안 됩니다.
콜백. 작업을 대기열에 넣거나 일시 중지하기 전에 `owned_bytes`에 복사하세요.
코루틴. `append_received_data()`는 해당 복사를 동기식으로 수행합니다.

동일한 `scatter_view`, 커서 및 파서 코드는 향후 재사용 가능
`ntl::ndis` 어댑터. 기본 WFP 및 NDIS 콜백/수명 래퍼는 그대로 유지됩니다.
보유, 복제, 반환, 일시 중지 및 삽입 규칙이 동일하기 때문에 분리됩니다.
교환할 수 없습니다.

## 제한된 코루틴 관찰

`<ntl/net/io/async_byte_stream>`는 고정 용량의 단일 판독기 링을 제공합니다.
순차 읽기로 더 명확한 프로토콜 코드:

```cpp
auto header = co_await stream.read_exactly<message_header>(
    {std::chrono::milliseconds(250)});
if (!header)
  co_return;

const auto body_size = decode_size(*header);
auto body = ntl::net::owned_bytes::try_allocate(
    body_size, ntl::net::buffer_limits{4096});
if (!body)
  co_return;

const ntl::status read =
    co_await stream.read_exactly_borrowed(body->span());
```

생산자는 `append_received_data(scatter_view)`를 호출합니다. 이 함수는 반환하기 전에
조각을 비페이징 저장소로 복사하고 용량 초과를 거부하며 빌린 네이티브 포인터를
보관하지 않습니다. 대기 중인 연속 실행은 시스템 작업 큐를 통해
`PASSIVE_LEVEL`에서 재개합니다. EOF, 취소, 시간 초과, 용량 초과, 다른 reader와의
경쟁은 `NTSTATUS`로 보고합니다. 작업 슬롯 두 개를 사용하므로 두 번째 read가
완료돼도 첫 번째 read를 재개 중인 작업 항목을 재사용하지 않습니다.

코루틴 프레임은 드라이버의 작업 또는 흐름 수명이 소유합니다. 대상 span과 프레임은
중단 상태에서도 살아 있어야 합니다. 해체 시 소유자는 `cancel_and_drain()`을
`co_await`한 뒤에만 스트림과 작업을 해제합니다. drain 연속 실행은 항상 별도의
`PASSIVE_LEVEL` 작업 항목에서 재개하므로, 현재 재개 중인 read 연속 실행에서
요청해도 안전합니다. 연속 실행이 큐에 있는 동안 작업을 파괴하면 안 됩니다.

`ntl::wfp::stream_reader`는 `stream_event`를 이 크기 제한 reader로 자동 복사하고,
연결 해제·중단·데이터 종료 시 닫습니다. 이는 비동기 관찰 전용 어댑터입니다. 원래
WFP 바이트를 보류하거나 유지하지 않으므로, 코루틴이 나중에 classify 콜백에서 이미
허용한 바이트를 차단할 수는 없습니다. 비동기 결과를 판정에 반영하는 WFP 경로는
`stream_result::defer()`, `cloned_stream_data`,
`stream_injection_site` 및 `stream_injector`; 시간 초과 및 메모리 제한
오류 정책은 페일오픈(fail-open) 또는 페일클로즈(fail-close)를 명시적으로 선택해야 합니다.

컨트롤러 측에는 별도의 사용자 모드 기본 요소가 있습니다.
`<ntl/net/io/async_socket>`. 중첩된 Winsock과 IOCP를 사용하여 다음을 제공합니다.
`co_await read_some_borrowed()`, `read_exactly_borrowed()` 및 `write_all()`.
빌린 이름은 대상 범위 수명을 명시적으로 만듭니다. 공유하지 않습니다
`async_byte_stream`를 사용하는 커널 풀, IRQL 또는 콜백 수명 기계.
`<ntl/net/io/async_framed_stream>`는 제한된 호출자 선택 메시지 프레이머를 추가합니다.
부분적이고 과도하게 읽은 TCP 바이트를 유지하고 완전한 소유만 생성합니다.
메시지. 길이 접두사, 구분 기호, 사용자 정의 프레이밍 및 디코더 어댑터는 다음과 같습니다.
[콘텐츠 검사 및 프레이밍](./inspection.ko-KR.md)에 설명되어 있습니다.
`<ntl/net/tls/stream>`는 별도의 Schannel 클라이언트/서버 전송을 제공합니다. 참조
[사용자 모드 Schannel TLS 스트림](./tls-stream.ko-KR.md).
스트림 편집 컨트롤러는 실제 루프백 트래픽에 대해 이러한 대기자를 사용합니다.
버퍼, 작업에 대해서는 [사용자 모드 코루틴 소켓](./async-socket.ko-KR.md)을 참조하세요.
취소 및 완료 컨텍스트 수명 규칙.

## 트랜잭션 사용자 모드 정책

`policy_session`는 수명을 명시적인 구성 선택으로 만듭니다. 안`ephemeral()` 세션은 다음과 같은 경우 BFE가 제거하는 프로세스 범위 개체를 설치합니다.
컨트롤러가 종료되거나 엔진 연결이 끊깁니다.

```cpp
auto session = ntl::wfp::policy_session::ephemeral(
    L"my product policy");
session.install([](ntl::wfp::policy_transaction& tx) {
  const auto provider = tx.add_provider(/* provider_spec */);
  const auto sublayer = tx.add_sublayer(provider, /* sublayer_spec */);
  const auto callout =
      tx.add_callout<layer>(provider, /* callout_spec<layer> */);

  ntl::wfp::filter_builder<layer> filter(
      /* filter_key<layer> */, L"Block selected TCP port");
  filter.protocol_equal(IPPROTO_TCP)
      .remote_address_equal(
          ntl::wfp::ipv4_address::from_octets(127, 0, 0, 1))
      .remote_port_equal(443);
  tx.add_filter(sublayer, callout, filter);
});
```

정상적인 반환 커밋. 모든 예외는 중단됩니다. 공급자, 하위 레이어 및 콜아웃
참조는 세대 ID가 있는 트랜잭션 범위 기능입니다.
두 트랜잭션 또는 공급자의 참조를 네이티브보다 먼저 던지는 혼합
필터 호출.

컨트롤러 또는 BFE 재시작이 지속되어야 하는 제품 정책의 경우
`persistent()` 세션을 실행하고 그래프 키를 한 번 선언합니다.

```cpp
ntl::wfp::policy_manifest manifest;
manifest.include(provider_key)
    .include(sublayer_key)
    .include(callout_key)
    .include(filter_key);

auto policy = ntl::wfp::policy_session::persistent(
    L"my persistent product policy");
policy.reconcile(manifest, [](ntl::wfp::policy_transaction& tx) {
  // Add the complete replacement graph.
});

const auto health = policy.health(manifest);
if (!health.healthy())
  policy.reconcile(manifest, write_complete_policy);

policy.uninstall(manifest); // explicit product uninstall
```

`reconcile()`는 매니페스트의 이전 필터, 콜아웃, 하위 레이어를 제거합니다.
및 공급자를 종속성 순서대로 설치하고 완전한 교체를 설치합니다.
동일한 기본 트랜잭션. 커밋하기 전에 NTL은 작성자가
매니페스트에서 선언한 키를 정확하게 생성했습니다. 생략되거나 선언되지 않은
객체가 트랜잭션을 중단합니다. `install()`는 임시 전용이지만
`reconcile()` 및 `uninstall()`는 영구 전용이므로 수명은
시공 후 실수로 선택되었습니다. `health()`가 개체 누락을 보고합니다.
정책을 변경하지 않고 키를 사용할 수 있습니다. `reconnect()`는 BFE 엔진을 다시 엽니다.
그런 다음 영구 호출자는 `health()` 또는 `reconcile()`를 실행합니다. 영구 공급자
`provider_spec::service_name`에 소유 서비스 이름을 지정할 수 있습니다.

정책 빌더는 기본 작업을 수정합니다.

| 빌더 | 레이어/용도 | 네이티브 액션 |
| --- | --- | --- |
| `filter_builder<Layer>` | ALE 인증 | 종료 + 조치 지우기 권한 |
| `inspection_filter_builder<Layer>` | 관찰만 | 점검; 콜백은 계속할 수만 있습니다 |
| `arbitration_filter_builder<Layer>` | 흐름/패킷 장애 시 종료 또는 조건부 결정 | 알려지지 않은; 콜백은 계속/허용/차단될 수 있습니다 |
| `packet_filter_builder<Layer>` | 데이터그램/전송 패킷 결정 | 종료 + 조치 지우기 권한 |
| `local_udp_proxy_reply_filter_builder<Layer>` | 루프백 `OUTBOUND_IPPACKET` 프록시 응답 복원 | 종료; 콜아웃이 없어도 관련 없는 트래픽은 계속 사용 가능 |
| `stream_filter_builder<Layer>` | 스트림 검사, 편집, 연결 제어 | 알 수 없음 |

공개 엔진 핸들, commit/abort 메서드, action-type 필드, 네이티브 조건 배열은
제공하지 않습니다.

`local_udp_proxy_reply_filter_builder`는 의도적으로 범용 패킷 빌더보다 좁습니다.
`OUTBOUND_IPPACKET`에는 프로토콜이나 포트 정책 조건이 없으므로 이 빌더는 레이어를
IPv4/IPv6 outbound IP로 고정하고 해당 주소 계열의 loopback 주소를 설치하며,
콜아웃 컨텍스트로 0이 아닌 프록시 포트를 요구하고, 사용할 수 없는 콜아웃 선택을
숨깁니다. 대응 콜아웃은 무언가를 흡수하기 전에 UDP 헤더와 포트를 파싱합니다.
콜아웃이 없어도 관련 없는 loopback 트래픽은 차단하지 않습니다.

일반 애플리케이션이 흐름·데이터그램·역방향 객체 여섯 개를 직접 조립할 필요는
없습니다. `transparent_udp_proxy_policy::install()`은 완전한 정책을 소유하고,
`add_to()`는 같은 분리 불가능 그래프를 기존 provider와 sublayer에 추가합니다.
`transparent_udp_proxy_service`는 이에 대응하는 듀얼 스택 콜아웃, 튜플 테이블,
주입, 콜백 rundown, 멱등적 닫기를 소유합니다. 제한 없는 outbound-IP 패킷 빌더는
`ntl::wfp::advanced`에서만 제공하므로 기본 API로 광범위한 역방향 후크를 만들 수
없습니다.

## 형식화된 조건

조건 메서드는 선택한 WFP 레이어가 해당 네이티브 필드를 지원할 때만 존재합니다.
예를 들어 ALE 레이어는 애플리케이션, 사용자, 패키지, 프로토콜, 주소, 포트 조건을
제공하지만 stream 레이어는 프로토콜을 제공하지 않습니다. MAC 레이어는 MAC,
EtherType, VLAN, 인터페이스 필드를 제공합니다. 따라서 유효하지 않은 레이어/조건
조합은 컴파일 단계에서 실패합니다.

주소와 ID에는 명시적인 소유 유형이 있습니다.

- `ipv4_address::from_octets()` 및 `ipv4_network`;
- `ipv6_address` 및 `ipv6_network`;
- `mac_address`;
- `user_identity` 및 `package_identity`.

접두사 길이, 방향 값, 빈 플래그 마스크, VLAN 식별자, SID
유효성 및 중복 기본 필드는 BFE가 호출되기 전에 유효성이 검사됩니다.
`icmp_equal(type, code)`는 프로토콜, ICMP 유형 및 ICMP 코드를 원자적으로 추가하므로
호출자는 실수로 프로토콜 간 규칙을 만들 수 없습니다.

## 진단 및 이벤트 원격 측정

`policy_session::inspect_filter()` 및 `enumerate_filters<Layer>()` 반환
제한된 포인터 없는 정책 스냅샷. 애플리케이션 Blob은 다음과 같이 표시됩니다.
유지된 실행 파일 경로 대신 크기와 해시를 사용합니다.

`network_event_monitor`는 WFP 분류 삭제 및 IPsec 삭제 이벤트를 구독합니다.
미리 할당된 경계 링에 복사합니다.

```cpp
ntl::wfp::network_event_monitor events({
    .maximum_queued_events = 2048,
    .manage_collection_state = false,
});

ntl::wfp::network_event_snapshot event;
if (events.wait_pop(event, std::chrono::seconds(1))) {
  // Correlate event.filter_id and event.layer_id with policy diagnostics.
}
```

콜백 경로는 대기열 할당을 수행하지 않습니다. 링 오버플로가 새로 추가되었습니다.
`dropped_by_limit`를 기록하고 증가시킵니다. 컬렉션 활성화는
시스템 전체 BFE 옵션이므로 모니터는 기본적으로 이를 변경하지 않습니다. 세트
`manage_collection_state`는 이 구성 요소가 전역 설정을 소유한 경우에만 해당합니다.
이전 값은 `stop()`에 의해 복원됩니다.

## 수명 및 IRQL

- 정책 관리 및 콜아웃 등록/등록 해제는
  `PASSIVE_LEVEL`.
- 분류, 흐름 연관, 패킷 참조/복제, 스트림 복사/복제 및
  주입 제출은 WFP 계층 계약을 따르며 다음과 같이 실행될 수 있습니다.
  `DISPATCH_LEVEL`. 해당 경로를 상주하고, 할당을 인식하고, 비차단하고,
  예외가 없습니다.
- 인젝터 퍼사드의 소멸은 IRQL과 무관합니다. 수락된 완료 작업이 공유 런다운
  상태를 소유하며, 네이티브 핸들 소멸은 런타임의 통합 PASSIVE 정리 도메인으로
  넘겨집니다.
- `callout_driver::add()`는 콜백 개체와 명시적으로 바인딩된 모든 개체를 소유합니다.
  `shared_ptr` 상태. `close()`는 새 콜백 항목을 거부하고 콜백을 배출합니다.
  및 흐름 컨텍스트를 모두 소진한 뒤 콜아웃을 역순으로 등록 해제하고, 이름 없는
  네트워크 장치를 삭제합니다. 복사된 퍼사드 중 어느 것에서 호출해도 멱등성을
  유지하며, 내부 종료 대기나 등록 해제가 실패하면 해당 네이티브 오류를
  보고합니다. 일반 `PASSIVE_LEVEL` 호출자는 완료된 결과를 받습니다. 드라이버
  자체 콜백이나 `APC_LEVEL`/`DISPATCH_LEVEL`에서 호출하면
  성공 상태인 `STATUS_PENDING` 닫기 요청을 반환합니다. 런타임은 소유자를 유지하고
  결합된 `PASSIVE_LEVEL` 작업자에서 같은 종료 처리를 완료합니다.
- `flow_target`은 등록 범위 기능입니다. 이를 소유한 `callout_driver`가 닫히기
  시작한 뒤 연결을 시도하면 `STATUS_DELETE_PENDING`을 반환합니다.
- 마지막 퍼사드를 `PASSIVE_LEVEL`보다 높은 IRQL에서 해제하면 네이티브 정리를
  런타임의 PASSIVE 정리 도메인으로 넘깁니다. 드라이버가 분리된 정리 작업 항목을
  큐에 넣거나 콜백 rundown을 직접 관리할 필요는 없습니다.

## 검증

[`examples/wfp/kernel/ale-connect-block`](../../examples/wfp/kernel/ale-connect-block)은
첫 번째 런타임 예제입니다. [한국어 설명](../../examples/wfp/kernel/ale-connect-block/README.ko-KR.md)은
드라이버, 컨트롤러, WFP 엔진과 9단계 실행 순서를 설명합니다. 이 예제는
`ALE_AUTH_CONNECT_V4` 콜아웃을 등록하고 하나의 임시 트랜잭션에서 모든 정책
객체를 설치해, 선택한 loopback TCP 연결이 거부되는지 확인합니다. 세션을 닫은
뒤에는 연결성이 복원되는지도 확인합니다. 런타임 테스트는 영구 manifest 조정,
설치 컨트롤러 종료, 새 연결에서 상태와 정책 적용 지속 여부 확인, 그래프의 명시적
제거까지 수행합니다.

[`test/wfp/compile`](../../test/wfp/compile)는 모든 유형의 레이어 패밀리를 컴파일합니다.
흐름 컨텍스트 전송, 스트림 상태 머신, ALE 보류 소유권 및
`/W4 /WX`를 사용하는 세 가지 비동기 인젝터 유형. 16개의 CTest 의미 체계계약은 모든 IPv4/IPv6 접두사 길이를 포함하여 디버그/릴리스에서 실행됩니다.
결정론적 단편 프레이밍/검색 입력, 파서 퍼즈 계약 및
브라우저 HTTP/3 계약.

고급 VM 게이트는 선택한 샘플만 빌드하고 패키징합니다. 는
듀얼 스택 정책 게이트는 `datagram-proxy`, `async-inspection`를 로드합니다.
`flow-monitor`, `udp-content-filter` 및 `tcp-content-filter`는 아래에서 함께 사용됩니다.
Driver Verifier는 샘플당 20개의 컨트롤러 반복을 실행합니다. 전체
회귀는 선택된 모든 고급 샘플을 함께 실행할 수 있습니다. IPv4 및 IPv6
리디렉션, 지연된 결정, 관찰, 콘텐츠 판정, 엔드포인트 폐쇄,
정책 이후 복원이 게이트에서 실행됩니다. 두 개의 콘텐츠 필터 샘플
독립적으로 증명하다
사용자 모드 코루틴은 완전한 UDP 데이터그램 또는 완전한 UDP 데이터그램을 결정할 수 있습니다.
명시적으로 선택된 TCP 응용 프로토콜의 메시지를 수신하지 않고
기본 WFP 실행 권한. TCP 블록은 흐름을 삭제합니다.
연결-리디렉션 샘플은 별도의 프록시 경로인 드라이버를 증명합니다.
선택한 TCP 연결을 리디렉션하면 앱은 원래 끝점을 캡처하고
불투명한 WFP 레코드와 두 개의 IOCP 코루틴이 바이트 스트림을 전달하기 전에 릴레이합니다.
임시 정책을 제거하면 직접 연결이 복원됩니다.
TLS 검사 프록시는 두 가지로 동일한 안전한 리디렉션 핸드오프를 구성합니다.
사용자 모드 Schannel 세션. 조각난 ClientHello를 관찰하고 다음을 선택합니다.
CA가 서명한 SNI별 leaf를 cache하고 크기가 제한된 HTTP/1.1 평문을 framing합니다.
요청, 허용 및 차단 결과를 모두 증명하고 신뢰 저장소를 떠납니다.
변경되지 않으며 정책 제거 후 직접 TLS 연결을 복원합니다.
`browser-https-inspection` 프로젝트는 독립적인 WFP 키와 서비스를 사용합니다.
이름. 해당 사용자 서비스는 Schannel HTTP/1.1 및 HTTP/2와 MsQuic을 종료합니다.
HTTP/3은 디코딩된 데이터에 하나의 소유 검사/재작성 정책을 적용합니다.
메시지. 커널 대응 부분은 WSK와 동일한 의미론적 정책을 실행합니다.
커널 Schannel 및 MsQuic NMR 백엔드. 런타임 래퍼는 다음을 관찰합니다.
프로필을 생성하지 않고 이미 실행 중인 정확한 브라우저 실행 파일, 실행
또는 브라우저를 종료하거나 플래그를 추가하고 자체 HTML 로그 정리를 수행합니다. 지역
제어된 원본 테스트는 결정적입니다. 외부 원본 HTTP/3 프로브는 다음과 같습니다.
주변 네트워크가 QUIC을 차단할 수 있으므로 별도로 분리하세요.
게이트는 조각화된 UDP NBL/MDL 계약도 실행하고, 로드/언로드 횟수와 충돌
이벤트 및 덤프를 검사하며, 호출자가 제공한 정확한 Driver Verifier 설정이
바이트 단위로 변경되지 않았는지 확인합니다. 크기가 제한된 코루틴 판독기
계약은 샘플이 아니라 전용 커널 계약 드라이버에서 검증합니다.
실행기는 VM을 시작·재설정·되돌리기·재부팅하지 않으며 Driver Verifier 설정도
변경하지 않습니다. 운영자는 실행 전에 필요한 드라이버 서명 부팅 옵션을 직접
선택합니다. Low Resources Simulation에는 운영자가 별도로 준비하고 제어하는 부팅
환경과 독립된 실행 증거가 필요하며, 일반 Driver Verifier 게이트의 통과만으로
저자원 시험까지 통과했다고 간주하지 않습니다. 이 별도 게이트는 커널 브라우저
검사 경로에서 실제 의도적 할당 실패 증가, fail-closed 정리, 새 충돌과 덤프 없음,
Verifier 설정 불변을 확인하며 통과했습니다.

Microsoft의 네트워크/트랜스 샘플에 대한 매핑 및 정확한 런타임 상태
에서 유지됩니다
[`test/wfp/WDK-SAMPLE-COVERAGE.md`](../../test/wfp/WDK-SAMPLE-COVERAGE.ko-KR.md).
