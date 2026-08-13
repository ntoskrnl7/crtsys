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

빌드 통합은 Windows 8 이상을 대상으로 하며 WFP를 선택하면 `fwpkclnt.lib`를
연결합니다. Visual Studio/NuGet 사용자는 **crtsys WDM 진입점** 속성 페이지에서
**NTL WFP**를 선택하거나 이에 대응하는 MSBuild 속성을 설정합니다.

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

NuGet `NTL WFP` 프로젝트는 패키지에서 검토된 커널 zlib/Brotli 아카이브를 자동으로
연결합니다. 소스 기반 CMake 프로젝트는 필요할 때만 선택합니다.

```cmake
crtsys_add_driver(
  my_callout WFP NTL KERNEL_CONTENT_CODECS src/main.cpp)
```

커널 드라이버는 옵션 하나로 고정된 MsQuic ABI, Windows 10 version 2004 대상 및
NMR 클라이언트 import를 선택합니다.

```cmake
crtsys_add_driver(
  my_h3_callout
  WFP NTL KERNEL_MSQUIC KERNEL_CONTENT_CODECS
  src/main.cpp)
```

`KERNEL_MSQUIC`는 공급자가 아니라 헤더와 `netio.lib`를 제공합니다. 고정 ABI만
필요한 사용자 모드 CMake 대상은 `crtsys_add_ntl_msquic_headers()`를 호출하고
`crtsys_ntl_msquic_headers`를 연결할 수 있습니다. NuGet과 사전 빌드 오프라인
번들은 SHA-256으로 검증한 같은 헤더를 포함하고 include 디렉터리를 자동으로
추가합니다. 사용자 애플리케이션은 호환되는 `msquic.dll`을 배포합니다. 커널 NuGet
사용자는 `<CrtSysUseNtlKernelMsQuic>true</CrtSysUseNtlKernelMsQuic>`를 선택해
Windows 10 version 2004 이상 계약과 `netio.lib`를 사용하고, 명시적으로 설치된
호환 MsQuic NMR 공급자에 바인딩합니다. 빌드 과정은 어느 공급자도 설치하거나
시작하지 않습니다.

## API에 의해 시행되는 규칙

### 레이어와 키는 원시 GUID가 되지 않습니다.

제공자, 하위 레이어, 콜아웃 및 필터 키는 고유한 유형입니다. 콜아웃 및
필터 키에는 레이어 유형도 포함되어 있습니다.

```cpp
using layer = ntl::wfp::layers::ale_auth_connect_v4;

constexpr GUID native_callout = /* project-owned stable GUID */;
constexpr ntl::wfp::arbitrating_callout_key<layer> callout(native_callout);
```

`stream_v4` 키를 `ale_auth_connect_v4` 등록이나 필터에 전달할 수 없습니다.
네이티브 GUID 접근은 비공개이며 등록 및 정책 작성기만 사용할 수 있습니다.

### 분류 콜백은 필터에서 허용하는 결정을 반환합니다.

일반 classify 콜백은 복사할 수 없고 콜백 범위에서만 유효한 읽기 전용
`classify_event<Layer>`를 받습니다. 콜아웃 키와 콜백 결과는 같은 네이티브 WFP
action 계약을 인코드합니다.

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

콜백은 `FWPS_CLASSIFY_OUT0`에 접근할 수 없습니다. trampoline이
`FWPS_RIGHT_ACTION_WRITE`, clear-action-right, veto 및 absorb 의미를 적용합니다.
컨트롤러와 드라이버는 같은 타입이 지정된 키를 사용해야 합니다.

- `inspection_callout_key` / `add_inspection()`: `void` 관찰자 콜백;
  WFP 분류는 자동으로 계속됩니다.
- `terminating_callout_key` / `terminating_decision`: 허용 또는 차단
- `arbitrating_callout_key` / `arbitration_decision`: 계속, 허용 또는
  블록(`FWP_ACTION_CALLOUT_UNKNOWN`).

`stream_callout_key`는 `stream_result` 및 고정된 stream UNKNOWN action과 짝을
이룹니다. 관찰 전용 stream 콜백은 대신 `inspection_callout_key`와
`add_stream_inspection()`을 사용하며 `void`를 반환합니다. 콜백은 `noexcept`여야
합니다. enforcement 콜백은 키가 선택한 decision 형식을 정확히 반환해야 하고,
inspection 콜백은 정확히 `void`를 반환해야 합니다. 컨트롤러 필터, 드라이버 등록
또는 콜백 반환 형식이 서로 맞지 않으면 컴파일되지 않습니다.

지원되는 결정 레이어는 다음과 같습니다.

- `ale_connect_redirect_v4` 및 `ale_connect_redirect_v6`;
- `ale_auth_connect_v4` 및 `ale_auth_connect_v6`;
- `ale_auth_recv_accept_v4` 및 `ale_auth_recv_accept_v6`;
- `ale_flow_established_v4` 및 `ale_flow_established_v6`;
- `datagram_data_v4` 및 `datagram_data_v6`
- inbound/outbound transport v4 및 v6
- `outbound_ip_packet_v4` 및 `outbound_ip_packet_v6`.

### 연결 리디렉션에는 하나의 합법적인 변형 경로가 있습니다.

`ALE_CONNECT_REDIRECT`에는 쓰기 가능한 layer data가 필요하지만 일반 콜백은 여전히
`FWPS_CONNECT_REQUEST0`이나 `FWPS_CLASSIFY_OUT0`에 접근할 수 없습니다. 드라이버는
캐시된 redirect handle 하나를 만들고 전체 작업을 위임합니다.

```cpp
auto redirector =
    ntl::wfp::connect_redirector::try_create(provider_key);

return redirector->redirect(
    event, ntl::wfp::local_proxy_target{proxy_pid, proxy_port});
```

이 작업은 같은 핸들이 이미 리디렉션한 연결을 감지하고, 쓰기 가능한 데이터를
성공적으로 얻을 때마다 `FwpsApplyModifiedLayerData0`을 정확히 한 번 짝지어
호출합니다. 원래 엔드포인트는 WFP 소유 컨텍스트로 넘기며 실패 시 차단합니다.
정책 쪽에서는 `connect_redirect_filter_builder`를 사용해야 합니다. 생성자는 프록시
PID와 host-order 포트를 요구하고 terminating action과 raw-context 인코딩을
고정합니다.

수락하는 프록시는 일치하는 핸드오프를 캡처하고 아웃바운드 구간을 엽니다.

```cpp
auto handoff =
    ntl::wfp::redirected_connection::capture(accepted_socket);
SOCKET outbound = handoff.connect_original();
```

`connect_original()`은 원래 목적지에 연결하기 전에 WFP의 불투명 redirect record를
붙입니다. 그러면 연결 attribution이 유지되고 커널 loop 검사가 프록시의 outbound
연결을 허용합니다. 이 API는 TCP byte-stream 프록시 경계일 뿐 TLS를 복호화하거나
애플리케이션 메시지 codec을 정의하지 않습니다. 사용자 모드 프록시는 이 handoff를
`<ntl/net/tls/stream>`과 결합해 accepted leg에서 TLS를 종료하고 outbound leg를
보호할 수 있습니다. 그래도 WFP callout에는 TLS 평문이나 키가 보이지 않습니다.

### 스트림 콜백의 결과 유형이 다릅니다

stream classify에는 일반 permit/block decision으로 표현할 수 없는 유효한 출력
조합이 있습니다. 따라서 `add_stream()`은 `stream_result`를 반환하는 콜백만
받습니다.

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

factory는 `permit(bytes)`, `block(bytes)`, `need_more(minimum)`, `defer()`,
`drop_connection()`, `allow_connection()`입니다. 어댑터는 classify action, stream
action, required/enforced count 및 action-right 검사를 함께 묶습니다. 적용 바이트
수는 전달된 stream 길이로 제한합니다. inspection filter가 편집기로 바뀌지 못하게
하고, WFP 버퍼 제한이나 no-more-data 상태에서 `need_more`가 반복되면 전체 버퍼를
fail-close 방식으로 차단합니다. 또한 outbound defer를 거부하고, native filter의
action이 UNKNOWN일 때만 drop-connection을 내보냅니다.

`stream_data_view::bytes()`는 native NBL/MDL 조각을 보는, 할당 없는
`scatter_view`입니다. `copy_to()`는 편리한 평탄화 경로로 유지됩니다.
`cloned_stream_data`는 이동 전용 out-of-band 경로이며 항상
`FwpsDiscardClonedStreamData0`을 사용하므로 일반 NBL clone과 혼동할 수 없습니다.

### 흐름 컨텍스트는 소유권을 한 번 이전합니다.

`callout_driver::add_stream<Context>()`와 `add_flow_context<Context>()`는
`flow_target<Layer, Context>`를 반환합니다. 대상은 같은 layer와 context 형식만
받습니다.

```cpp
auto state = std::make_unique<flow_state>();
const ntl::status status =
    stream_target.associate(flow_handle, std::move(state));
```

성공하면 내부 등록 보유자로 소유권이 이전되고 WFP는 그 보유자의 불투명 ID만
받습니다. 등록은 모든 보유자를 추적하며 flow-delete trampoline이 컨텍스트를
정확히 한 번 삭제합니다. 실패하면 실패한 이전 경로가 컨텍스트를 삭제합니다.
컨텍스트 저장소와 소멸자는 대상 layer가 flow를 삭제하는 IRQL에서 안전해야 하며,
소멸자는 `noexcept`여야 합니다.

데이터그램 흐름 컨텍스트는 다음 형식의 콜백을 사용합니다.

```cpp
decision callback(const classify_event<datagram_data_v4>&,
                  proxy_context*) noexcept;
```

원시 정수 흐름 컨텍스트는 애플리케이션 코드에 의해 캐스팅되지 않습니다.

### 지연된 패킷에는 명시적인 소유권이 있습니다.

- `borrowed_packet`는 콜백에만 유효합니다.
- `referenced_packet`는 WFP NBL 참조를 보유합니다.
- `cloned_packet`은 `FwpsAllocateCloneNetBufferList0`이 만든 NBL을 소유합니다.
- `cloned_stream_data`는 `FwpsCloneStreamData0`가 생성한 체인을 소유하고 있습니다.
- `pended_operation`은 이미 완료하지 않은 ALE 작업을 소멸자에서 완료합니다.

`network_injector`, `transport_injector`, `stream_injector`는 각각 공유 injection
상태를 소유합니다. 비동기 주입이 성공하면 패킷 소유자를 완료 컨텍스트로
이전합니다. `injection_limits::maximum_in_flight`는 제출 전에 적용되며, 한도
소진 시 호출자의 패킷을 소비하지 않고 `STATUS_QUOTA_EXCEEDED`를 반환합니다.
`close()`나 소멸은 새 작업을 거부하지만, 수락한 모든 완료가 drain될 때까지
네이티브 핸들은 살아 있습니다. APC 또는 DISPATCH_LEVEL에서 마지막 참조가
해제되면 런타임의 PASSIVE 정리 도메인으로 넘깁니다. 따라서 호출자는 정리 작업을
예약하거나 소멸 IRQL을 기억할 필요가 없습니다.

outbound transport injection도 이동 전용 `transport_send_request`를 사용합니다.
이 객체는 endpoint, address family, scope, compartment와 함께 원격 주소 및 보조
제어 데이터를 deep-copy하거나 인수합니다. `transport_injector::inject_send()`는
clone과 request를 같은 완료 컨텍스트로 옮깁니다. WFP가 비동기 완료 시점까지
`FWPS_TRANSPORT_SEND_PARAMS0`이 참조하는 버퍼를 유지하므로 반드시 필요합니다.
따라서 NTL API는 스택에 둔 네이티브 parameter block을 노출하지 않습니다.

```cpp
auto request = ntl::wfp::transport_send_request::try_copy(
    endpoint, AF_INET6, compartment, remote_address, remote_scope,
    control_data, ntl::net::buffer_limits{4096});
if (!request)
  return ntl::wfp::terminating_decision::block_and_absorb;

const auto injected = injector.inject_send(
    std::move(clone), std::move(*request));
```

동기 제출이 실패하면 완료 컨텍스트를 즉시 소멸시킵니다. 성공적으로 제출하면 WFP
완료 콜백에서 소멸시킵니다. injector를 재설정하면 새 제출을 중단합니다. rundown과
PASSIVE 정리는 수락한 패킷 소유자와 네이티브 상태를 콜백이 끝날 때까지 유지합니다.
어느 경로도 호출자가 버퍼 수명을 관리하거나 별도 정리 분기를 둘 필요가 없습니다.

`stream_event`는 타입이 지정된 `stream_injection_site<Layer>`를 발급할 수 있습니다.
이 객체는 WFP가 제공한 flow, callout, layer 및 stream flag를 캡처하되 해당 ID의
생성자를 노출하지 않습니다. 이 site는 `continue_deferred()`를 호출할 권한도
나타냅니다.

## 수동 MDL 탐색 없이 단편화된 바이트

`<ntl/net/buffer/scatter_view>`는 공통의 네이티브 중립 바이트 계층입니다.
의도적으로 WFP나 NDIS 콜백 형식을 포함하지 않습니다.

- `scatter_view`는 읽기 전용 조각을 빌리고 수명을 연장하지 않습니다.
- `mutable_scatter_view`는 소유형 또는 명시적으로 변경 가능한 어댑터만 반환합니다.
- `borrowed_byte_cursor`는 제한된 순차 및 빅엔디안 읽기를 수행합니다.
- `scan_bytes()`는 평탄화하지 않고 조각 경계를 넘어 고정 토큰을 찾습니다.
- `owned_bytes`는 필수 `buffer_limits` 검사를 거친 이동 전용 비페이지 deep copy입니다.

패킷 래퍼는 차이점을 직접적으로 노출합니다.

```cpp
const auto borrowed = event.packet();
ntl::net::borrowed_byte_cursor header(borrowed.bytes()); // callback lifetime only

auto saved = borrowed.try_copy(ntl::net::buffer_limits{64 * 1024});
if (!saved)
  return ntl::wfp::terminating_decision::block_and_absorb;
```

`borrowed_packet::bytes()`와 `stream_data_view::bytes()`는 모든
`NET_BUFFER_LIST`, `NET_BUFFER` 및 MDL 조각을 순회합니다. UDP 포트 필드가 MDL
경계를 가로질러도 `cloned_packet::rewrite_udp_destination_port()`는 해당 필드를
수정하고 checksum을 지웁니다. `NdisGetDataBuffer()`가 연속 메모리를 반환한다고
가정할 필요가 없습니다.

뷰를 제공한 객체와 backing 저장소는 작업 전체에서 유효해야 합니다. classify
event에서 얻은 뷰를 콜백 뒤까지 보관하면 안 됩니다. 작업을 큐에 넣거나 코루틴을
중단하기 전에 `owned_bytes`로 복사하십시오. `append_received_data()`는 이 복사를
동기식으로 수행합니다.

같은 `scatter_view`, cursor 및 parser 코드는 향후 `ntl::ndis` 어댑터에서도 재사용할
수 있습니다. 다만 retain, clone, return, pause 및 injection 규칙은 서로 바꿔 쓸 수
없으므로 네이티브 WFP와 NDIS의 콜백/수명 래퍼는 계속 분리합니다.

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

코루틴 프레임은 드라이버의 task 또는 flow 수명이 소유합니다. 대상 span과 프레임은
중단 중에도 살아 있어야 합니다. 해체할 때 소유자는 `cancel_and_drain()`을
`co_await`한 뒤에만 stream과 task를 해제합니다. drain continuation은 항상 별도의
`PASSIVE_LEVEL` 작업 항목에서 재개하므로 현재 재개 중인 read continuation이
요청해도 안전합니다. continuation이 큐에 있는 동안 task를 소멸시키면 안 됩니다.

`ntl::wfp::stream_reader`는 `stream_event`를 이 크기 제한 reader로 자동 복사하고,
연결 해제·중단·데이터 종료 시 닫습니다. 이는 비동기 관찰 전용 어댑터입니다. 원래
WFP 바이트를 보류하거나 유지하지 않으므로, 코루틴이 나중에 classify 콜백에서 이미
허용한 바이트를 차단할 수는 없습니다. 비동기 결과를 판정에 반영하는 WFP 경로는
`stream_result::defer()`, `cloned_stream_data`,
`stream_injection_site` 및 `stream_injector`; 시간 초과 및 메모리 제한
오류 정책은 페일오픈(fail-open) 또는 페일클로즈(fail-close)를 명시적으로 선택해야 합니다.

컨트롤러 쪽에는 별도의 사용자 모드 기본 요소인 `<ntl/net/io/async_socket>`이
있습니다. overlapped Winsock과 IOCP를 사용해 `co_await read_some_borrowed()`,
`read_exactly_borrowed()`, `write_all()`을 제공합니다. 이름의 `borrowed`는 대상 span의
수명을 명시합니다. 이 API는 `async_byte_stream`의 커널 풀, IRQL 또는 콜백 수명
구조를 공유하지 않습니다. `<ntl/net/io/async_framed_stream>`은 호출자가 선택하는
크기 제한 message framer를 추가하고, 덜 읽거나 더 읽은 TCP 바이트를 유지하며,
완성된 소유형 메시지만 반환합니다. 길이 접두사, 구분자, 사용자 정의 framing 및
decoder adapter는 [콘텐츠 검사 및 프레이밍](./inspection.ko-KR.md)에 설명되어
있습니다. `<ntl/net/tls/stream>`은 별도의 Schannel 클라이언트/서버 전송을
제공합니다. [사용자 모드 Schannel TLS 스트림](./tls-stream.ko-KR.md)을 참고하십시오.
stream-edit 컨트롤러는 실제 loopback 트래픽에 이 awaiter들을 사용합니다. 버퍼,
task, 취소 및 완료 컨텍스트 수명 규칙은
[사용자 모드 코루틴 소켓](./async-socket.ko-KR.md)을 참고하십시오.

## 트랜잭션 사용자 모드 정책

`policy_session`은 수명을 명시적인 생성 옵션으로 만듭니다. `ephemeral()` 세션은
프로세스 범위 객체를 설치하며, 컨트롤러가 종료되거나 엔진 연결이 끊기면 BFE가
이를 제거합니다.

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

정상적으로 반환하면 commit하고 예외가 발생하면 abort합니다. provider, sublayer,
callout 참조는 generation ID가 있는 transaction 범위 capability입니다. 서로 다른
transaction이나 provider의 참조를 섞으면 네이티브 필터 호출 전에 예외를
던집니다.

컨트롤러나 BFE가 다시 시작되어도 유지해야 하는 제품 정책에는 `persistent()`
세션을 만들고 graph key를 한 번 선언합니다.

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

`reconcile()`은 manifest의 기존 filter, callout, sublayer, provider를 의존성
순서대로 제거하고 같은 네이티브 transaction에 완전한 대체 graph를 설치합니다.
commit 전에 NTL은 작성기가 manifest에 선언된 키를 정확히 만들었는지 검증합니다.
누락되거나 선언되지 않은 객체가 있으면 transaction을 abort합니다. `install()`은
ephemeral 전용이고 `reconcile()`과 `uninstall()`은 persistent 전용이므로 생성 후
실수로 수명을 바꿀 수 없습니다. `health()`는 정책을 변경하지 않고 누락된 객체
키를 보고합니다. `reconnect()`는 BFE engine을 다시 열며, persistent 호출자는 그
뒤에 `health()` 또는 `reconcile()`을 실행합니다. persistent provider는
`provider_spec::service_name`에 소유 서비스 이름을 지정할 수 있습니다.

정책 빌더는 기본 작업을 수정합니다.

| 빌더 | 레이어/용도 | 네이티브 액션 |
| --- | --- | --- |
| `filter_builder<Layer>` | ALE 권한 검사 | terminating + clear action right |
| `inspection_filter_builder<Layer>` | 관찰 전용 | inspection; 콜백은 continue만 가능 |
| `arbitration_filter_builder<Layer>` | flow/packet fail-close 또는 조건부 판정 | unknown; 콜백은 continue/permit/block 가능 |
| `packet_filter_builder<Layer>` | datagram/transport packet 판정 | terminating + clear action right |
| `local_udp_proxy_reply_filter_builder<Layer>` | loopback `OUTBOUND_IPPACKET` 프록시 응답 복원 | terminating; callout이 없어도 관련 없는 트래픽은 계속 처리 가능 |
| `stream_filter_builder<Layer>` | stream 검사, 편집 및 연결 제어 | unknown |

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

## 타입이 지정된 조건

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

접두사 길이, 방향 값, 빈 플래그 마스크, VLAN ID, SID 유효성 및 중복 네이티브
필드는 BFE를 호출하기 전에 검증합니다.
`icmp_equal(type, code)`는 프로토콜, ICMP 유형 및 ICMP 코드를 원자적으로 추가하므로
호출자는 실수로 프로토콜 간 규칙을 만들 수 없습니다.

## 진단 및 이벤트 원격 측정

`policy_session::inspect_filter()`와 `enumerate_filters<Layer>()`는 크기가 제한되고
포인터가 없는 정책 스냅샷을 반환합니다. 애플리케이션 blob은 실행 파일 경로를
보관하지 않고 크기와 해시로 나타냅니다.

`network_event_monitor`는 WFP classify-drop 및 IPsec-drop 이벤트를 구독해 미리
할당한 크기 제한 ring에 복사합니다.

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

콜백 경로에서는 큐 메모리를 할당하지 않습니다. ring이 넘치면 새 레코드를 버리고
`dropped_by_limit`를 증가시킵니다. 수집 활성화는 시스템 전체 BFE 옵션이므로
monitor는 기본적으로 변경하지 않습니다. 이 구성 요소가 전역 설정을 소유할 때만
`manage_collection_state`를 설정하십시오. `stop()`은 이전 값을 복원합니다.

## 수명 및 IRQL

- 정책 관리와 콜아웃 등록 및 등록 해제는 `PASSIVE_LEVEL`에서 수행합니다.
- classify, flow 연결, 패킷 참조/복제, 스트림 복사/복제 및 injection 제출은 해당
  WFP 계층의 계약을 따르며 `DISPATCH_LEVEL`에서 실행될 수 있습니다. 이 경로의
  코드는 상주해야 하고, 할당을 주의해서 사용하며, 블로킹하거나 예외를 던지면 안
  됩니다.
- 인젝터 래퍼의 소멸은 IRQL과 무관합니다. 수락된 완료 작업이 공유 런다운
  상태를 소유하며, 네이티브 핸들 소멸은 런타임의 통합 PASSIVE 정리 도메인으로
  넘겨집니다.
- `callout_driver::add()`는 콜백 객체와 명시적으로 바인딩한 모든 `shared_ptr`
  상태를 소유합니다. `close()`는 새 콜백 진입을 거부하고 콜백 및 flow context가
  모두 끝나기를 기다린 뒤 콜아웃을 역순으로 등록 해제하고, 이름 없는
  네트워크 장치를 삭제합니다. 복사된 래퍼 중 어느 것에서 호출해도 멱등성을
  유지하며, 내부 종료 대기나 등록 해제가 실패하면 해당 네이티브 오류를
  보고합니다. 일반 `PASSIVE_LEVEL` 호출자는 완료된 결과를 받습니다. 드라이버
  자체 콜백이나 `APC_LEVEL`/`DISPATCH_LEVEL`에서 호출하면
  성공 상태인 `STATUS_PENDING` 닫기 요청을 반환합니다. 런타임은 소유자를 유지하고
  결합된 `PASSIVE_LEVEL` 작업자에서 같은 종료 처리를 완료합니다.
- `flow_target`은 등록 범위 기능입니다. 이를 소유한 `callout_driver`가 닫히기
  시작한 뒤 연결을 시도하면 `STATUS_DELETE_PENDING`을 반환합니다.
- 마지막 래퍼를 `PASSIVE_LEVEL`보다 높은 IRQL에서 해제하면 네이티브 정리를
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

[`test/wfp/compile`](../../test/wfp/compile)는 모든 타입이 지정된 layer 계열, flow context
이전, stream 상태 머신, ALE 보류 소유권 및 세 비동기 injector 형식을 `/W4 /WX`로
컴파일합니다. 16개의 CTest 의미 계약은 Debug/Release에서 실행되며 모든 IPv4/IPv6
접두사 길이, 결정적인 조각 framing/검색 입력, parser fuzz 계약 및 브라우저 HTTP/3
계약을 포함합니다.

고급 VM 게이트는 선택한 예제만 빌드하고 패키징합니다. dual-stack 정책 게이트는
`datagram-proxy`, `async-inspection`, `flow-monitor`, `udp-content-filter`,
`tcp-content-filter`를 Driver Verifier 아래에서 함께 로드하고 예제마다 컨트롤러를
20회 반복 실행합니다. 전체 회귀에서는 선택한 모든 고급 예제를 함께 실행할 수
있습니다. 게이트는 IPv4/IPv6 리디렉션, 지연 판정, 관찰, 콘텐츠 판정, 엔드포인트
닫기 및 정책 제거 후 복원을 실행합니다. 두 콘텐츠 필터 예제는 사용자 모드
코루틴이 네이티브 WFP action 권한을 받지 않고도 완전한 UDP datagram 또는 명시적으로
선택한 TCP 애플리케이션 프로토콜의 완전한 메시지를 판정할 수 있음을 각각
입증합니다. TCP 차단은 flow를 삭제합니다.

connect-redirect 예제는 별도의 프록시 경로를 입증합니다. 드라이버가 선택한 TCP
연결을 리디렉션하고, 앱이 원래 엔드포인트와 불투명 WFP record를 캡처한 뒤 IOCP
코루틴 두 개가 byte stream을 relay합니다. ephemeral 정책을 제거하면 직접 연결이
복원됩니다.

TLS inspection-proxy는 같은 안전한 redirect handoff를 사용자 모드 Schannel 세션
두 개와 결합합니다. 조각난 ClientHello를 관찰하고 CA가 서명한 SNI별 leaf를
선택·캐시하며, 크기가 제한된 HTTP/1.1 평문 요청을 framing합니다. permit과 block
결과를 모두 검증하고 신뢰 저장소를 변경하지 않으며 정책 제거 후 직접 TLS 연결을
복원합니다.

`browser-https-inspection` 프로젝트는 독립된 WFP 키와 서비스 이름을 사용합니다.
사용자 서비스는 Schannel HTTP/1.1·HTTP/2와 MsQuic HTTP/3을 종료한 뒤 디코드된
메시지에 소유형 검사/재작성 정책 하나를 적용합니다. 커널 대응 예제는 WSK, 커널
Schannel 및 MsQuic NMR 백엔드에서 같은 의미 정책을 실행합니다. 런타임 래퍼는
프로필을 만들거나 브라우저를 실행·종료하거나 플래그를 추가하지 않고, 이미 실행
중인 정확한 브라우저 실행 파일을 관찰하며 HTML 로그 정리도 직접 소유합니다. 로컬
통제 오리진 테스트는 결정적이고, 외부 오리진 HTTP/3 probe는 주변 네트워크에서
QUIC을 차단할 수 있으므로 별도로 분리합니다.

게이트는 조각난 UDP NBL/MDL 계약도 실행하고, 로드/언로드 횟수와 crash event 및
dump를 확인하며, 호출자가 제공한 Driver Verifier 설정이 바이트 단위로 그대로인지
검증합니다. 크기 제한 코루틴 reader 계약은 예제가 아니라 전용 kernel-contract
드라이버에서 검증합니다. 실행기는 VM을 시작·초기화·되돌리기·재부팅하지 않으며
Driver Verifier 설정도 변경하지 않습니다. 운영자는 실행 전에 필요한 드라이버
서명 부팅 옵션을 직접 선택합니다. Low Resources Simulation은 운영자가 별도로
준비하고 제어하는 부팅 환경과 별도 실행 증거가 필요하며, 일반 Driver Verifier
게이트 통과가 저자원 테스트 통과를 뜻하지는 않습니다. 별도 게이트는 커널 브라우저
검사 경로에서 의도적인 할당 실패가 실제로 증가하는지, fail-close 정리가 되는지,
새 crash나 dump가 없는지, Verifier 설정이 그대로인지 확인했으며 통과했습니다.

Microsoft network/trans 예제와의 대응 관계 및 정확한 런타임 상태는
[`test/wfp/WDK-SAMPLE-COVERAGE.md`](../../test/wfp/WDK-SAMPLE-COVERAGE.ko-KR.md)에
관리합니다.
