# NTL RPC

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL RPC는 하나의 공유 매크로 선언에서 `DeviceIoControl` 기반 커널 콜백
디스패처와 사용자 모드 래퍼 함수를 생성합니다. 구현의 기반은 IOCTL ID, 인수 형식,
반환 형식, 직렬화된 최대 응답 크기를 담은 타입이 지정된 메서드 설명자입니다. 직렬화에는
`zpp::serializer`를 사용합니다.

빌드 가능한 드라이버/앱 쌍에 대해서는 다음을 참조하세요.
[`examples/ntl-rpc-driver`](../../examples/ntl-rpc-driver).

헤더:

- [`include/ntl/rpc/common`](../../include/ntl/rpc/common)
- [`include/ntl/rpc/server`](../../include/ntl/rpc/server)
- [`include/ntl/rpc/client`](../../include/ntl/rpc/client)
- [`include/ntl/rpc/coroutine`](../../include/ntl/rpc/coroutine), 옵션 C++20
- [`include/ntl/rpc/registry_notification_store`](../../include/ntl/rpc/registry_notification_store), 선택적 레지스트리 지속성

## 공유 계약 및 구현

```cpp
// shared/demo_rpc.hpp
#pragma once

#include <cstdint>
#include <vector>

NTL_RPC_BEGIN_CONTRACT(demo_rpc, 3, 0x1ull)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, left, int, right, {
  return left + right;
})

NTL_ADD_CALLBACK_1(
    demo_rpc,
    NTL_RPC_BOUNDED_RESPONSE(64 * 1024, std::vector<std::uint32_t>),
    read_values, std::uint32_t, count, {
      if (count > 4096)
        throw ntl::exception(STATUS_INVALID_PARAMETER, "count is too large");
      return std::vector<std::uint32_t>(count, 42);
    })

NTL_RPC_END(demo_rpc)
```

드라이버에서는 이 헤더보다 먼저 `<ntl/rpc/server>`를 포함하고, 앱에서는
`<ntl/rpc/client>`를 포함합니다. 드라이버에서 각 매크로 본문은 등록된 커널 콜백이
됩니다. 앱에서는 같은 선언으로 `demo_rpc::add(left, right)` 같은 타입이 지정된 래퍼를
만들며, 커널 전용 본문은 앱에 컴파일되지 않습니다.

`NTL_ADD_CALLBACK_0`부터 `NTL_ADD_CALLBACK_5`까지의 접미사는 콜백 인수 개수를
나타냅니다. 매크로는 이 값으로 양쪽의 이름 있는 매개변수와 직렬화 코드를
생성합니다. 이 기본 형식은 공유 스키마의 행에서 ID를 파생하므로 드라이버와 앱이
같은 계약 헤더로 빌드될 때 편리합니다.

공유 스키마의 형식이나 순서를 바꾸더라도 메서드 ID를 그대로 유지해야 한다면
`NTL_ADD_CALLBACK_ID_0`부터 `NTL_ADD_CALLBACK_ID_5`까지를 사용하십시오. 이는
ABI 안정성을 선택적으로 제어하는 기능이며, 일반적인 공유 헤더 사용에 반드시
필요한 형식은 아닙니다.

실행 시간이 긴 콜백에는 `NTL_ADD_CALLBACK_CONTEXT_0`부터
`NTL_ADD_CALLBACK_CONTEXT_5`까지 또는 명시적 ID를 받는
`NTL_ADD_CALLBACK_CONTEXT_ID_*` 형식을 사용할 수 있습니다. 콜백 이름 다음
인수는 커널에서만 쓰는 `ntl::rpc::call_context` 변수의 이름입니다.

```cpp
NTL_ADD_CALLBACK_CONTEXT_ID_1(
    demo_rpc, 0x902, std::uint32_t, calculate, call,
    std::uint32_t, count, {
      std::uint32_t result = 0;
      for (std::uint32_t index = 0; index != count; ++index) {
        call.throw_if_cancelled();
        result += calculate_one(index);
      }
      return result;
    })
```

`call`이라는 이름을 명시하면 콜백 본문에서 사용할 요청 컨텍스트가 드러나므로
숨은 매크로 식별자에 의존하지 않습니다. 이 컨텍스트는 직렬화되지 않으며 사용자
모드 함수 시그니처에도 포함되지 않습니다. 앱에는 여전히
`demo_rpc::calculate(count)`와 `demo_rpc::calculate_1_method`가 생성되며, 메서드
ID와 wire payload는 `NTL_ADD_CALLBACK_ID_1`을 사용했을 때와 같습니다.

매크로로 선언한 메서드에 역직렬화 전 권한 검사도 필요하다면
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_0`부터
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_5`까지 또는 그 명시적 ID 형식을
사용하십시오. 콜백 이름 다음 인수는 서버 측 정책 callable이며, 그다음 인수는
메서드 콜백에서 사용할 `call_context`의 이름입니다.

```cpp
NTSTATUS authorize_user_mode(
    const ntl::rpc::call_context& caller) noexcept {
  return caller.is_user_mode() ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_1(
    demo_rpc, 0x903, std::uint32_t, protected_echo,
    authorize_user_mode, call, std::uint32_t, value, {
      call.throw_if_cancelled();
      return value;
    })
```

정책 callable은 원래 요청자의 컨텍스트를 받고 `NTSTATUS` 또는 `ntl::status`를
반환해야 합니다. 이는 서버 전용 토큰이므로 클라이언트 확장에서는 버리고, 일반
콜백 선언과 같은 동기, 비동기, stop-token 및 코루틴 래퍼를 생성합니다. 정책이
실패 상태를 반환하면 인수 바이트를 디코드하거나 메모리를 할당하기 전에 요청을
거부합니다.

명시적 메서드 ID는 엔드포인트 안에서 안정적이고 고유해야 하며 공급업체 IOCTL
함수 범위인 `0x800`부터 `0xFFC`까지를 사용해야 합니다. NTL은 세션 제어에
`0xFFD`, 알림 수신에 `0xFFE`, 계약 검색에 `0xFFF`를 예약합니다. 클라이언트
인수는 직렬화 전에 선언된 인수 형식으로 변환되므로, 고정 너비 메서드 선언에
네이티브 너비의 호출자 형식이 실수로 인코드되지 않습니다.

사용자 정의 객체에는 `zpp::serializer` 직렬화 함수를 제공하십시오.
[`test/cmake/common/rpc.hpp`](../../test/cmake/common/rpc.hpp)에서 사용하는 검증된
`point` 클래스가 그 패턴을 보여 줍니다.

## 커널 서버

```cpp
#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>

#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto server = demo_rpc::init(driver);

  driver.on_unload([server]() mutable { server.reset(); });
  return ntl::status::ok();
}
```

`demo_rpc::init()`는 `\Device\demo_rpc`를 만들고 공유 선언의 모든 콜백을
등록합니다. 반환된 `std::shared_ptr<ntl::rpc::server>`는 엔드포인트 수명 동안
유지하십시오. 같은 메서드 ID를 두 번 등록하면 기존 콜백을 조용히 교체하지 않고
실패합니다. 매크로가 생성한 `init()`는 등록 후 `start()`를 호출합니다. 알림이나
세션 후크를 매크로 메서드 등록 뒤이면서 시작 전인 시점에 추가해야 한다면 생성된
`demo_rpc::make_server()`를 사용한 뒤 `start()`를 명시적으로 호출하십시오.
`ntl::rpc::make_server()`를 직접 사용하는 경우도 같은 규칙을 따릅니다. 시작 후
디스패치 테이블은 불변이므로 활성 호출과 경쟁할 수 있는 늦은 등록은 거부됩니다.

서버 콜백은 드라이버의 device-control 디스패치 경로에서 실행됩니다. 짧게
유지하고, 할당 전에 의미 한도를 검증하며, 다른 드라이버 제어 콜백과 같은 IRQL
및 수명 규칙을 따르십시오. 서버 종료는 새 호출을 거부하고 이미 런다운 보호를
획득한 콜백이 끝나기를 기다립니다. 서버 자신의 콜백에서 `stop()`을 호출하면 그
콜백이 반환되기를 스스로 기다리게 되므로 호출하면 안 됩니다.

`stop()`은 새 RPC 디스패치를 거부하지만 제어 장치의 소유권은 서버에 남겨 둡니다.
위 예제처럼 드라이버가 언로드될 때까지 서버를 유지하십시오. 그러면 서버
소멸자가 NTL 장치와 C++ 확장 상태를 해제하기 전에 기존 사용자 핸들이 닫힐 수
있습니다.

## 사용자 모드 클라이언트

```cpp
#include <exception>
#include <iostream>

#include <ntl/rpc/client>

#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    ntl::rpc::client client(L"demo_rpc");
    if (!client)
      return 1;

    // Generated wrapper: opens the endpoint and performs one typed call.
    std::cout << demo_rpc::add(40, 2) << '\n';

    // Reusable handle: every generated callback also exposes its method object.
    const auto values = client.invoke(demo_rpc::read_values_1_method,
                                      std::uint32_t{16});
    return values.size() == 16 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "RPC failed: " << error.what() << '\n';
    return 1;
  }
}
```

생성된 래퍼는 가끔 호출할 때 가장 짧은 경로입니다. 반복 호출에서는 장치를 다시
열지 않도록 `ntl::rpc::client`와 생성된 `<name>_<arity>_method` 객체를
재사용하십시오. `invoke()`는 메서드 객체에서 반환 형식을 도출하므로 호출자가
ID나 반환 형식을 다시 지정하지 않아도 됩니다.

## 비동기 호출, 시간 초과 및 취소

비동기 호출은 엔드포인트에서 선택적으로 활성화합니다. 서버는 애플리케이션 메서드
IRP를 보류하고 PASSIVE_LEVEL 시스템 작업 항목에서 콜백을 호출합니다.
계약 검색은 동기식으로 유지됩니다.

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.asynchronous().max_pending_calls(64);

// Macro contracts accept the same options while retaining their generated
// callback registration.
auto server = demo_rpc::init(driver, options);
```

보류 호출 제한은 유지할 IRP, 비페이지 요청 상태 및 대기 중인 작업의 수를
제한합니다. 기본값은 `server_options::default_max_pending_calls`입니다. 콜백의
비용이 크다면 제품에 맞게 더 작은 값을 선택하십시오. 제한을 넘는 호출은
애플리케이션 콜백을 실행하기 전에 실패합니다.

사용자 모드 클라이언트는 작업이 완료될 때까지 모든 입출력 버퍼, 이벤트 및
`OVERLAPPED` 구조체를 소유합니다.

```cpp
using namespace std::chrono_literals;

ntl::rpc::client client(L"demo_rpc");
auto call = client.invoke_async(demo_rpc::read_values_1_method,
                                std::uint32_t{16});

if (call.wait_for(250ms) == ntl::rpc::async_wait_status::timeout) {
  (void)call.cancel();
}

const auto values = call.get(); // waits, then deserializes or throws
```

`wait_for()`가 `timeout`을 보고해도 진행 중인 요청은 해제되지 않습니다.
호출자는 계속 기다리거나 `cancel()`을 호출하거나 `async_call`을 소멸시킬 수
있습니다. 소멸 시 취소를 요청하고 완료 처리가 끝날 때까지 기다린 뒤 버퍼를
해제합니다. `async_call`과 이를 생성한 `client`는 같은 장치 핸들을 각각
유지하므로 `async_call`이 `client`보다 오래 살아도 됩니다.

`CancelIoEx`로 임의의 커널 C++ 코드를 강제 중단할 수는 없습니다. 큐에 있던 콜백이
시작되기 전에 취소가 먼저 처리되면 NTL은 그 콜백을 실행하지 않습니다. 콜백이 이미
실행 중이면 반환할 때까지 두되 출력을 버리고 IRP를 `STATUS_CANCELLED`로
완료합니다. 이후 `get()`은 `ERROR_OPERATION_ABORTED`를 담은
`std::system_error`를 던집니다. 따라서 콜백 작업은 유한해야 하며 취소를 스레드
종료로 취급하면 안 됩니다.

오래 실행되는 콜백은 선언한 RPC 인수 앞에 `ntl::rpc::call_context`를 받아
협력적 취소를 지원할 수 있습니다.

```cpp
server->on(read_values,
    [](ntl::rpc::call_context context, std::uint32_t count) {
      std::vector<std::uint32_t> result;
      result.reserve(count);
      for (std::uint32_t index = 0; index != count; ++index) {
        context.throw_if_cancelled();
        result.push_back(read_one_value(index));
      }
      return result;
});
```
같은 콜백을 앞서 설명한 `NTL_ADD_CALLBACK_CONTEXT_*` 계열의 공유 매크로
계약으로 선언할 수도 있습니다. 컨텍스트 인식 선언만으로 엔드포인트가 비동기가
되지는 않습니다. 완전한 비동기 및 협력적 취소 동작에는 다음 세 요소가 모두
필요합니다.

1. `server_options::asynchronous()`를 사용하여 엔드포인트를 구성합니다.
2. `client::invoke_async()`로 호출을 시작해 대기, 시간 초과 또는 취소할 수 있는
   `async_call`을 얻습니다.
3. 오래 실행되는 콜백 작업에서 `call_context::cancelled()` 또는
   `throw_if_cancelled()`를 사용해 이미 실행 중인 콜백도 신속히 중지할 수 있게
   합니다.

각 콜백 매크로는 두 가지 양식을 모두 생성합니다. `read_values`라는 메서드의 경우,
`read_values(args...)`는 동기식 편의 함수이며
`read_values_async(args...)`는 `async_call<T>`를 반환합니다.

```cpp
auto call = demo_rpc::read_values_async(std::uint32_t{16});
```

동기 편의 함수와 마찬가지로 이 형식도 독립적인 호출을 위해 명명된 엔드포인트를
엽니다. 호출이 많은 코드는 `ntl::rpc::client` 하나를 유지하고 생성된
`<name>_<arity>_method` 설명자를 `client.invoke()` 또는
`client.invoke_async()`에 전달할 수 있습니다.

`cancelled()`는 요청이 취소되거나 NTL 엔드포인트가 실제로 `stop()`을 시작하면
true가 됩니다. `throw_if_cancelled()`는 해당 RPC를 `STATUS_CANCELLED`로
종료하며 앱에서는 `ERROR_OPERATION_ABORTED`로 관찰됩니다. 작은 작업 하나하나마다
검사하지 않으면서도 취소 지연 시간을 제한할 수 있는 폴링 간격을 선택하십시오.

컨텍스트 매개변수는 선택 사항입니다. 기존 `[](args...)` 콜백은 원래 동작을
유지하며 컨텍스트 인식 형식보다 먼저 선택됩니다. 앱이 열린 레거시 WDM 장치
핸들을 계속 소유하는 동안에는 SCM 서비스 중지 요청만으로 엔드포인트 종료가
시작되지 않습니다. `SERVICE_STOPPED`를 기다리기 전에 보류 중인 호출을 취소하고
클라이언트를 닫으십시오.

`call_context`는 콜백이 실행되는 동안에만 유효한 비소유 뷰입니다.
콜백보다 오래 지속될 수 있는 작업에서 이를 유지하거나 캡처하지 마세요.

동기식 `invoke()`는 일반 엔드포인트와 비동기 엔드포인트 모두에서 사용할 수
있습니다. 비동기 엔드포인트에서는 보류 작업이 끝나기를 기다리면서 기존의
타입이 지정된 반환 API를 그대로 유지합니다.

### C++20 중지 토큰 및 코루틴

기존 `invoke_async(method, args...)` 오버로드는 C++14 이상에서 계속 사용할 수
있습니다. C++20 클라이언트는 같은 작업을 stop token에 연결할 수 있습니다.

```cpp
#include <stop_token>

std::stop_source source;
auto call = demo_rpc::read_values_async(source.get_token(),
                                        std::uint32_t{16});

source.request_stop();
```

반환된 `async_call<T>`는 `std::stop_callback` 등록을 소유합니다. 중지 요청은
해당 작업에 대해서만 `CancelIoEx`를 호출합니다. 소멸 시 네이티브 작업 상태가
해제되기 전에 등록을 제거합니다. 토큰이 이미 중지된 상태라면 NTL은 RPC를
발행하지 않고 이미 취소된 작업을 반환합니다.

동일한 기본 작업을 기다리려면 선택적 C++20 어댑터를 포함하세요.

```cpp
#include <ntl/rpc/coroutine>

application_task<std::vector<std::uint32_t>>
read_async(std::stop_token token) {
  co_return co_await demo_rpc::read_values_async(token, std::uint32_t{16});
}
```

생성된 비동기 함수는 C++14 이상에서 사용할 수 있습니다. `std::stop_token`
오버로드와 직접 `co_await`하는 방식은 C++20이 필요합니다.
`<ntl/rpc/coroutine>`을 포함하면 `async_call<T>`을 await할 수 있습니다.

`<ntl/rpc/coroutine>`은 Windows 스레드 풀 대기를 사용해 `OVERLAPPED` 이벤트가
신호되면 코루틴을 재개합니다. 호출마다 대기 스레드를 하나씩 만들지 않으며,
`async_call<T>`이 보유한 요청 버퍼, 이벤트, 장치 핸들 및 `CancelIoEx` 소유권을
대체하지도 않습니다. 호출을 `co_await`로 이동해도 한 번만 소비할 수 있는
`get()` 의미는 유지됩니다.

C++ 표준은 범용 코루틴 `task<T>` 반환 형식이나 스케줄러를 제공하지 않습니다.
애플리케이션은 기존 작업 프레임워크와 함께 이 awaitable을 사용합니다. RPC
예제에는 자체 완결성을 위해 작은 최상위 작업 소유자가 들어 있습니다. RPC
awaiter가 아직 보류 중일 때 코루틴을 소멸시키면 이후 재개를 막고 해당 요청을
취소합니다. 다른 코루틴 awaitable과 마찬가지로 외부 작업 소유자는 자신이
조정하지 않는 재개와 동시에 같은 코루틴 프레임을 소멸시키면 안 됩니다.

`receive_async()`에도 같은 C++20 중지 토큰 오버로드를 사용할 수 있습니다.
C++14 및 C++17 번역 단위는 `<stop_token>`이나 `<coroutine>`을 포함하지 않고
기존 API와 ABI 인터페이스를 유지합니다. 내부 비동기 작업의 레이아웃도 모든 언어
모드에서 같으므로, 한 실행 파일에서 C++14 번역 단위와 중지 토큰 또는 코루틴
어댑터를 사용하는 C++20 번역 단위를 안전하게 링크할 수 있습니다.

## 커널-앱 알림

알림 채널은 드라이버와 앱이 공유하는 직렬화 가능 페이로드 하나를 정의합니다.
채널 ID는 고정 너비의 애플리케이션 ID이며, 네이티브 포인터나 아키텍처 너비의 값이
아닙니다.

```cpp
struct progress_event {
  std::uint64_t operation_id = 0;
  std::string phase;
  std::vector<std::uint32_t> values;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.operation_id, self.phase, self.values);
  }
};

constexpr auto progress =
    ntl::rpc::notification<0x1001, progress_event>{}
        .max_response_size<64 * 1024>()
        .max_decode_allocation<128 * 1024>();
```

모든 채널은 `start()` 전에 등록하십시오. 임의 페이로드를 직렬화하는 과정에서
CRT/STL 코드를 실행할 수 있으므로 알림 게시에는 `PASSIVE_LEVEL`이 필요합니다.

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.max_pending_notifications(32);

auto server = ntl::rpc::make_server(driver, options);
server->register_notification(progress);
server->start();

const auto status = server->try_notify(
    progress, progress_event{42, "indexing", {1, 2, 3}});
if (static_cast<NTSTATUS>(status) == STATUS_NOT_FOUND) {
  // No app currently has a receive queued. NTL does not buffer the event.
}
```

앱은 드라이버에게 이벤트 생성을 요청하기 전에 수신을 대기열에 넣습니다.

```cpp
using namespace std::chrono_literals;

ntl::rpc::client client(L"demo_rpc");
auto receive = client.receive_async(progress);

if (receive.wait_for(250ms) == ntl::rpc::async_wait_status::timeout)
  (void)receive.cancel();

const auto event = receive.get();
```

`receive(progress)`는 블로킹 형식입니다. `receive_async(progress)`는
`notification_wait<payload_type>`를 반환하며, `ready()`, `wait_for()`, `wait()`,
`cancel()`, `get()`에는 `async_call<T>`와 같은 소유권 규칙이 적용됩니다.

전송은 역호출 큐 방식입니다. 앱의 각 receive는 보류 `METHOD_BUFFERED` IRP 하나를
제공하고, 성공한 `try_notify()`는 일치하는 채널에서 가장 오래 기다린 receive 하나를
꺼냅니다. 의도적으로 큐 전달이며 broadcast 전달이 아닙니다. 따라서 독립 소비자가
여럿이면 같은 엔드포인트의 이벤트를 서로 경쟁해 받습니다. broadcast 의미가 필요한
제품은 엔드포인트를 분리하거나 애플리케이션 계약에 구독자 ID를 구현해야 합니다.

NTL은 취소 경쟁을 처리하기 위해 `IO_CSQ`를 사용합니다. `CancelIoEx`,
`notification_wait` 소멸, `IRP_MJ_CLEANUP`, 서버 종료는 각각 receive 하나를 정확히
한 번 큐에서 제거해 완료합니다. `stop()`은 새 receive를 먼저 거부하고 큐에 있는
모든 receive를 실패로 완료한 뒤 일반 RPC 콜백 rundown을 기다립니다.

앱이 열린 장치 핸들을 소유하는 동안에는 레거시 WDM 드라이버의 서비스 중지가
완료될 수 없습니다. `SERVICE_STOPPED`를 기다리기 전에 모든
`notification_wait`를 취소하거나 소멸시키고 해당 클라이언트를 닫으십시오. 그러면
`IRP_MJ_CLEANUP`이 앱의 보류 receive를 제거해 언로드를 시작할 수 있습니다. 서버
종료 시의 flush는 엔드포인트가 여전히 소유한 receive를 정리하는 최종 방어선입니다.

보류 receive 제한은 유지되는 IRP와 그 I/O 관리자 버퍼의 수를 제한합니다. 대기
중인 receive가 없으면 이벤트를 보관하지 않고 `try_notify()`가
`STATUS_NOT_FOUND`를 반환합니다. 따라서 드라이버는 제품 정책에 따라 이벤트를
버리거나, 집계하거나, 병합하거나, 별도 큐에 넣을 수 있습니다. 페이로드 바이트에는
추가 프레임워크 헤더가 없습니다. receive 요청에는 숨겨진 `std::uint32_t` 채널 ID
하나만 들어가므로 같은 x64 드라이버 계약을 x86 및 x64 앱에서 사용할 수 있습니다.

## 클라이언트 세션 및 신뢰성 알림

위의 알림 API는 의도적으로 일시적입니다. 앱이 먼저 receive를 큐에 넣으며, 대기
중인 receive가 없으면 드라이버가 이벤트를 버리거나 직접 처리합니다. 특정
클라이언트가 확인할 때까지 이벤트를 보존해야 한다면 선택적 클라이언트 세션을
사용하십시오.

서버를 시작하기 전에 채널 및 세션 정책을 등록하십시오.

```cpp
auto server = demo_rpc::make_server(driver, options);
server
    ->register_notification(progress)
    .on_session_open(
        [](ntl::rpc::client_session& session,
           const ntl::rpc::call_context&) -> NTSTATUS {
          session.state(std::make_shared<client_state>());
          return STATUS_SUCCESS;
        })
    .on_session_resume(
        [](ntl::rpc::client_session& session,
           const ntl::rpc::call_context& caller) -> NTSTATUS {
          return authorize_reconnect(session, caller);
        });
server->start();
```

`client_session::state()`는 핸들이 끊겼다가 다시 연결된 뒤에도 애플리케이션 소유의
타입이 지정된 상태를 보관합니다. 세션 후크는 `PASSIVE_LEVEL`에서 실행됩니다. 열기 및
재개 정책에 전달되는 `call_context`는 현재 요청자를 나타내므로, 다시 연결할 때
토큰만을 유일한 권한 판단으로 신뢰하지 않고 신원을 재검사할 수 있습니다.
`client_session::token()`을 사용하면 애플리케이션 후크가 외부 인증이나 구독
메타데이터를 복원할 수 있습니다. 이 토큰은 비밀로 취급하고 절대 로그에 남기지
마십시오. 콜백은 실행 중에만 `client_session` 포인터를 사용할 수 있으며 포인터를
보관하면 안 됩니다. 명시적 닫기와 보존 기간 만료는 세션에 연결된 활성 RPC 콜백이
끝나기를 기다린 뒤 `on_session_close()`를 실행합니다.

앱은 해당 세션을 명시적으로 생성하고 구독합니다.

```cpp
ntl::rpc::client client(L"demo_rpc");
const auto session = client.start_session();
client.subscribe(progress);

auto delivery = client.receive_reliable(progress);
process(delivery.payload());
client.acknowledge(progress, delivery);
```

드라이버는 해당 세션의 엔드포인트-로컬 숫자 ID를 대상으로 합니다.

```cpp
const auto status = server->try_notify(
    session_id, progress, progress_event{42, "indexing", {1, 2, 3}});
```

안정적인 전달에는 다음과 같은 규칙이 있습니다.

- `try_notify(session_id, ...)`는 해당 채널을 구독한 세션 하나에만 레코드 하나를
  직렬화하여 큐에 넣습니다. `PASSIVE_LEVEL`이 필요합니다.
- `receive_reliable()`과 `receive_reliable_async()`는 타입이 지정된 페이로드와 0이 아닌
  시퀀스 번호가 든 `notification_delivery<T>`를 반환합니다.
- 전달된 레코드는 `acknowledge()`가 성공할 때까지 세션에 남습니다. ACK 전에
  연결이 끊기면 in-flight 상태가 초기화되어, 다음에 토큰을 재개한 핸들이 같은
  시퀀스를 다시 받습니다.
- 장치 핸들을 소멸하거나 닫으면 세션 연결만 끊기며 재생 상태는 버리지 않습니다.
  `session.token`을 저장한 뒤 새로 연 클라이언트에서 `resume_session(token)`을
  호출하십시오.
- `close_session()`은 세션과 그 영속 레코드를 명시적으로 삭제합니다.
  해당 토큰은 나중에 재개될 수 없습니다.
- `unsubscribe()`는 보류 중인 reliable receive를 취소합니다. 해당 채널에 확인하지
  않은 레코드가 남아 있으면 조용한 데이터 손실을 막기 위해 구독 해제를 거부합니다.
  먼저 그 레코드를 처리하고 ACK하십시오.
- 세션별 및 엔드포인트 전체 큐 제한은 다음 함수로 구성합니다.
  `max_reliable_notifications_per_session()` 및
  `max_reliable_notifications()`. 큐가 가득 차면 `STATUS_DEVICE_BUSY`를 반환하여,
  커널 메모리를 무제한 할당하는 대신 제한된 backpressure를 제공합니다.

재연결 토큰은 무작위 128비트 capability입니다. 비밀로 취급하여 로그에 기록하거나
관련 없는 프로세스에 노출하지 마십시오. 엔드포인트 ACL과
`on_session_resume()`는 정책 경계를 유지합니다.

연결이 끊긴 인메모리 세션은 `session_retention_ms()`가 지나면 만료됩니다. 선택적으로
`notification_storage(std::shared_ptr<notification_store>)`를 사용해 수명을 더
오래 유지하는 저장소와 연결할 수 있습니다. 이 저장소는 게시 전의 직렬화된 레코드,
ACK에 따른 제거, 토큰 복원 및 명시적 세션 삭제를 전달받습니다. 저장소를 설치하지
않으면 저장소 I/O는 전혀 발생하지 않습니다. 저장소 후크는 내부 세션 락을 잡지
않은 채 `PASSIVE_LEVEL`에서 실행됩니다. 연결 끊김, 재시도 및 종료와 외부 저장소
오류가 경합할 수 있으므로 후크는 멱등이어야 합니다. 보존 기간이 끝나도 인메모리
사본만 해제할 뿐 `erase_session()`을 호출하지 않습니다. 따라서 외부 저장소가
확인하지 않은 레코드를 하나 이상 보관했다면 나중에 토큰을 복원할 수 있습니다.

레지스트리 기반 복구를 사용하려면 `<ntl/rpc/registry_notification_store>`를
포함하고 제공되는 어댑터를 명시적으로 설치하십시오.

```cpp
auto key = ntl::registry_key::create(
    L"\\Registry\\Machine\\Software\\DemoRpcNotifications",
    KEY_QUERY_VALUE | KEY_SET_VALUE);
if (!key)
  return key.status();

server->notification_storage(
    std::make_shared<ntl::rpc::registry_notification_store>(
        std::move(*key)));
```

어댑터는 세션마다 크기가 제한된 `REG_BINARY` 값 하나를 유지하고 ACK 후 레코드를
제거합니다. 호출자가 키, ACL, 휘발성/비휘발성 수명 및 정리 정책을 결정합니다.
복원 레코드는 어떤 순서로 제공해도 됩니다. NTL은 원래 세션 시퀀스로 정렬하고,
중복 시퀀스, 일반 알림의 terminal marker, 해당 스트림의 terminal record 뒤에 오는
stream record를 거부합니다. 따라서 저장소 구현이 일관성 없는 데이터를 반환해도
완료된 스트림이 실수로 다시 열리거나 뒤에 레코드가 추가되지 않습니다.

일시적 `receive()`/`try_notify()`와 세션 기반 reliable delivery는 같은 타입이 지정된
채널의 서로 다른 모드입니다. 기존의 일시적 알림 코드는 세션을 만들거나 암묵적으로
버퍼링을 얻지 않습니다.

## 타입이 지정된 스트리밍

NTL RPC 스트림은 세션 하나에 바인딩된 양방향 형식화 채널입니다. 두 방향은 서로
독립적입니다.

```text
app                                           driver
read_async() waits for output
write(upload) ------------------------------> upload callback
                   stream.write(download) <-- queues one output record
read completes
acknowledge(record) ------------------------> frees queue capacity
                   stream.complete() <------ queues the terminal record
read terminal record, ACK it, then close
```

앱은 업로드를 쓰는 동안 `read_async()`를 보류해 둘 수 있으므로 업로드와 다운로드가
동시에 진행될 수 있습니다. 현재 보류 중인 read가 없어도 다운로드는 사라지지
않습니다. 앱이 읽고 명시적으로 ACK할 때까지 reliable session 큐에 남습니다.
`complete()`와 `fail()` 역시 큐에 넣는 레코드이지, 암묵적인 닫기 작업이 아닙니다.

아래의 `upload_chunk::finish`는 NTL 전송 플래그가 아니라 애플리케이션 데이터입니다.
앱은 마지막 입력 청크를 알고 있으므로 `write()`를 호출하기 전에 이 필드를
설정합니다. 이 예제 프로토콜에서는 마지막 입력 뒤에 드라이버가 출력을 끝낼 수
있습니다. 수명이 긴 스트림이라면 이 필드를 생략하고 드라이버가 소유한 조건에 따라
종료할 수 있습니다.

직렬화 형식과 스트림 콜백은 공유 계약 헤더에서 한 번만 정의합니다.

```cpp
struct upload_chunk {
  std::uint64_t sequence;
  std::vector<std::uint32_t> values;
  bool finish;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.sequence, self.values, self.finish);
  }
};

struct download_chunk {
  std::uint64_t sequence;
  std::string text;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive& archive, Self& self) {
    archive(self.sequence, self.text);
  }
};

NTL_RPC_BEGIN_CONTRACT(demo_rpc, 1, 0)

NTL_ADD_STREAM_ID(demo_rpc, 0x920, records,
                  upload_chunk, upload,
                  download_chunk, stream, {
  stream.write(download_chunk{upload.sequence, "accepted"});
  if (upload.finish)
    stream.complete();
})

NTL_RPC_END(demo_rpc)
```

`<ntl/rpc/server>` 뒤에 포함하면 이 매크로가 드라이버 업로드 콜백을 등록합니다.
`stream`은 콜백 범위의 `stream_context`로, 현재 클라이언트 세션을 대상으로
`write()`, `complete()`, `fail()`, 취소 기능 및 원래 호출 컨텍스트를
제공합니다. `<ntl/rpc/client>` 뒤에 포함하면 같은 선언이
`demo_rpc::records_stream`과 `demo_rpc::records(client)` 열기 도우미를
생성합니다.

앱은 세션을 시작하고 생성된 스트림 객체를 연 뒤 쓰기 전에 읽기를
준비합니다. 그러면 두 독립 방향이 코드에 명확히 드러납니다.

```cpp
ntl::rpc::client client(L"demo_rpc");
(void)client.start_session();
auto records = demo_rpc::records(client);

auto pending_reply = records.read_async();
records.write(upload_chunk{1, {10, 20, 30}, true});

auto reply = pending_reply.get();
consume(reply.payload().value());
records.acknowledge(reply);

auto terminal = records.read();
if (!terminal.payload().is_completed())
  throw std::runtime_error("stream did not complete");
records.acknowledge(terminal);

records.close();
client.close_session();
```

모든 업로드에 역직렬화 전 권한 부여 정책이 필요하다면
`NTL_ADD_AUTHORIZED_STREAM_ID`를 사용하십시오. 요청자 정보를 캡처해 검사하는 보안
의미는 `on_authorized()`와 같으며, 생성되는 앱 API는 달라지지 않습니다.

계약에 사용자 정의 업로드·다운로드·디코드 제한이 필요하거나 공유 매크로 헤더로
등록을 표현할 수 없다면 `ntl::rpc::stream`, `server::on_stream()`,
`client::open_stream()` API를 직접 사용할 수도 있습니다.

`write_async()`와 `read_async()`는 일반 RPC 및 알림 호출과 같은 소유형 비동기
작업을 반환합니다. 따라서 `wait_for()`, `cancel()`, C++20 stop token 및 코루틴
규칙도 같습니다. 시간 초과 후에도 기반 I/O 요청은 살아 있습니다. 계속 기다리거나,
취소하거나, 소유자를 소멸시키십시오. `close()`나 장치 핸들 닫기 전에 처리 중인
read와 write를 취소하거나 모두 완료해야 합니다. ACK되지 않은 레코드가 있으면
닫기가 실패할 수 있으므로 `client_stream` 소멸자는 스트림을 조용히 닫지 않습니다.

드라이버 출력은 reliable notification 큐를 사용합니다. terminal record를 포함해
성공적으로 읽은 모든 레코드에 ACK해야 합니다. `try_complete()`는 성공 종료
레코드를, `try_fail()`은 WDK `NTSTATUS`가 든 실패 terminal record를 큐에 넣습니다.
ACK 전에 연결이 끊기면 `resume_session()` 후 같은 시퀀스를 다시 전달합니다.

terminal record를 큐에 넣은 뒤에는 NTL이 해당 세션과 스트림의 추가 업로드, 출력
청크 및 중복 terminal record를 거부합니다. 클라이언트가 terminal에 ACK하면 다음
read는 `STATUS_END_OF_FILE`로 실패하며, 그다음 클라이언트가 `close()`를 호출합니다.
닫기는 해당 스트림 인스턴스를 제거하므로 같은 세션에서 나중에 새 인스턴스를 열 수
있습니다. terminal ACK 전에 연결이 끊기면 재연결 후 재생할 terminal 시퀀스와 쓰기
불가 상태가 모두 보존됩니다.

생산자 하나가 순차적으로 실행한 호출은 그 순서대로 큐에 들어갑니다. 여러 콜백이
같은 세션과 스트림에 동시에 게시한다면 병합 순서를 정할 애플리케이션 시퀀스 필드를
사용하십시오. terminal record는 이미 시작된 출력 게시 뒤에서 기다리고 새 게시가
시작되는 것을 막습니다.

backpressure는 양방향 모두 제한됩니다.

- 다운로드에는 구성된 세션별 및 엔드포인트 전체 reliable 큐 제한이 적용됩니다.
  큐가 가득 차면 `try_write()`, `try_complete()`, `try_fail()`이
  `STATUS_DEVICE_BUSY`를 반환합니다. ACK하면 용량이 풀립니다. terminal enqueue가
  실패하면 예약을 롤백하므로 스트림은 계속 쓸 수 있고 생산자는 용량이 생긴 뒤
  다시 시도할 수 있습니다.
- 업로드는 일반적인 제한형 RPC 요청입니다. 직렬화된 요청 및 디코드 예산은 스트림
  설명자에서 가져오며, 비동기 동시성은
  `server_options::max_pending_calls()`로 제한됩니다.

### 제한된 배치 및 전달 우선순위

단일 레코드 `write()`, `read()`, `read_async()`, `acknowledge()` API가 기본입니다.
스트림은 요소 형식을 바꾸지 않고 IOCTL 하나로 직렬화된 레코드 여러 개를 옮길 수도
있습니다.

```cpp
std::vector<upload_chunk> uploads{first, second, last};
records.write_batch(uploads);

auto downloads = records.read_batch(4);
for (const auto& delivery : downloads.values()) {
  consume(delivery.payload());
}
records.acknowledge(downloads);
```

`write_batch()`는 벡터 순서대로 각 요소에 등록된 업로드 콜백을 한 번씩 호출하고,
요소 사이에서 협력적 취소를 검사합니다. 트랜잭션은 아니므로 뒤쪽 콜백이 실패해도
앞쪽 콜백의 효과는 롤백되지 않습니다. 빈 배치와 설명자의
`max_batch_records()`보다 큰 배치는 거부됩니다. 직렬화된 요청과 누적 디코드
할당도 일반 업로드 제한 안에 들어야 합니다.

`read_batch()`는 이미 준비된 레코드를 요청 수 이하로 반환하며 배치가 가득 찰
때까지 기다리지 않습니다. 반환된 각 레코드는 고유 시퀀스를 유지하며 ACK 전까지
재생할 수 있습니다. 배치 ACK 편의 함수는 해당 ACK들을 순서대로 보내지만 이것도
원자적이지 않으므로 오류가 앞서 성공한 ACK를 되돌리지 않습니다. 시간 초과,
`CancelIoEx`, stop token, 재연결, terminal 및 영속성 동작은 단일 reliable read와
같습니다.

컴파일 시간 제한의 기본값은 16이며 64를 초과할 수 없습니다.

```cpp
constexpr auto records =
    ntl::rpc::stream<0x920, upload_chunk, download_chunk>{}
        .with_batch_records<8>()
        .with_priority<ntl::rpc::delivery_priority::high>();
```

우선순위는 여러 구독 reliable 채널에 준비된 레코드 중에서 any-channel 배치 receive
하나가 선택할 때만 의미가 있습니다. 채널별 `read()`나 `read_batch()`는 이미
채널을 지정하므로 우선순위를 비교할 대상이 없습니다. 한 채널 안에서는 NTL이 항상
시퀀스/FIFO 순서를 보존합니다. any-channel 소비자는 다음과 같이 사용합니다.

```cpp
auto batch = client.receive_reliable_batch(8, 64 * 1024);
for (const auto& item : batch.items()) {
  if (item.id() == critical_events.id()) {
    auto event = item.decode(critical_events);
    process(event.payload());
  }
  client.acknowledge(item);
}
```

선택기는 `critical`, `high`, `normal`, `background` 순으로 비교하며, 우선순위가
같으면 세션 시퀀스로 선택합니다. 보류 배치는 레코드 하나만 준비되어도 즉시
완료하므로 앞으로 높은 우선순위 레코드가 올 가능성 때문에 낮은 우선순위 레코드를
지연하지 않습니다. `max_records`와 `max_bytes` 인수는 receive 하나에 사용할 커널
및 앱 메모리를 제한합니다.

이 스트림은 직렬화된 청크를 `METHOD_BUFFERED`로 전송하며 zero-copy 데이터 경로가
아닙니다. 공유 메모리나 MDL 기반 전송에는 서로 다른 매핑, 프로세스 종료, 취소 및
드라이버 언로드 소유권 요구 사항이 있습니다. 따라서 직렬화 스트림 계약을 몰래
바꾸지 않고 명시적인 선택형 데이터 플레인으로 제공합니다.

## 공유 메모리 데이터 플레인

처리량이 많은 고정 레이아웃 레코드에는 선택형 클라이언트 세션이
`client::register_shared_region()`으로 호출자 소유 메모리를 등록할 수 있습니다.
드라이버는 원래 요청자 프로세스 컨텍스트에서 페이지를 probe, 고정 및 매핑한 뒤
고정 너비 `region_handle`을 반환합니다. RPC 메서드는 `buffer_token` 값만 주고받고,
콜백은 드라이버 읽기 또는 쓰기 접근을 명시하여 `call_context::try_resolve()`로
해석합니다.

공통 `ntl::ipc::shared_ring<T, Capacity>` 레이아웃은 각 레코드를
`DeviceIoControl`로 직렬화하지 않고 제한된 SPSC backpressure를 제공합니다. 양방향
통신에는 방향마다 링 하나를 사용하십시오. 레코드 필드는 고정 너비여야 하며 포인터,
핸들, `size_t`, 문자열 또는 STL 컨테이너를 포함하면 안 됩니다.

공유 영역에는 클라이언트 세션이 필요합니다. 등록 해제, 연결 끊김, 세션 닫기,
보존 기간 만료 및 서버 중지 시 무효화됩니다. 세션별 개수 및 바이트 할당량은
`server_options`로 구성합니다. API와 수명 규칙은
[`IPC 공유 메모리`](./ipc.ko-KR.md)를, x64 드라이버와 x64/x86 클라이언트 VM 검증은
[`test/rpc/cross-bitness`](../../test/rpc/cross-bitness)를 참고하십시오.

## 계약 확인

앱은 엔드포인트를 연 뒤 공유 계약을 한 번 검증할 수 있습니다. 계약 검색은 모든
RPC 요청에 헤더를 붙이는 방식이 아니라 별도로 예약된 조회입니다.

```cpp
ntl::rpc::client client(L"demo_rpc");

ntl::rpc::contract_requirements requirements;
requirements
    .contract_version(demo_rpc::rpc_contract_version)
    .transport_features(ntl::rpc::transport_features::resource_limits |
                        ntl::rpc::transport_features::secure_endpoint)
    .capabilities(demo_rpc::rpc_capabilities)
    .method(demo_rpc::add_2_method)
    .method(demo_rpc::read_values_1_method);

const auto server_contract = client.require_contract(requirements);
```

`NTL_RPC_BEGIN_CONTRACT(Name, Version, Capabilities)`는 애플리케이션 계약 버전,
애플리케이션 정의 capability 비트, 등록된 모든 형식화 멤버를 게시합니다.
`NTL_RPC_BEGIN(Name)`은 간결한 형식이며 애플리케이션 capability 비트 없이 버전
`1`을 게시합니다.

보고된 값에는 별도의 의미가 있습니다.

- `protocol_version()`는 NTL wire 형식 버전이며 `require_contract()`가 자동으로
  검증합니다.
- `contract_version()`은 애플리케이션 의미 프로토콜 개정판입니다. 직렬화 필드
  형식은 그대로지만 동작 의미가 바뀔 때 올립니다.
- `transport_feature_mask()`는 리소스 제한, 엔드포인트 보안, 변경 불가능한
  디스패치, rundown 종료 같은 NTL 보호 기능을 나타냅니다.
- `capability_mask()`에는 애플리케이션 정의 선택적 기능 비트가 포함되어 있습니다.
- `method_ids()`는 `start()` 이전에 등록된 정렬된 메소드 집합입니다.
- `members()`에는 메서드, 알림, 스트림의 고정 너비 한도와 자동 파생 wire 스키마
  지문이 들어 있습니다.
- `schema_hash()`는 이 멤버들에서 파생한 집계 지문이며 애플리케이션이 직접
  제공하지 않습니다.

NTL은 반환값, 인수, 페이로드, 업로드, 다운로드 형식에서 지문을 파생합니다. 사용자
정의 객체는 기존 `static serialize(Archive&, Self&)` 필드 목록을 재사용하므로
C++14 이상에서도 두 번째 스키마 선언이 필요하지 않습니다. 컴파일러 형식 이름과
호스트 포인터 너비는 해시에 포함하지 않으므로 고정 너비 wire 필드는 x86과 x64
클라이언트에서 동일하게 비교됩니다.

`query_contract()`는 애플리케이션 정책을 적용하지 않고 이 메타데이터를 반환합니다.
`require_contract()`는 선택한 요구 사항을 검사하고 불일치하면
`ntl::rpc::contract_mismatch`를 던집니다. `reason()`, `expected()`, `actual()`은
프로토콜 버전, 애플리케이션 버전, 전송 기능, capability, 스키마 해시, 누락 메서드
오류를 설명합니다. 따라서 비즈니스 메서드를 호출하기 전에 드라이버와 앱의 버전
불일치를 명확히 보고할 수 있습니다.

공유 스키마 순서를 바꾼 뒤에도 메서드 ID를 안정적으로 유지해야 한다면 명시적 ID를
받는 `NTL_ADD_CALLBACK_ID_*`를 사용합니다. 행 번호에서 파생한 ID는 양쪽을 항상 같은
헤더로 빌드할 때 편리하지만, 소스 순서를 바꾸면 ID도 변경됩니다.

## 제한된 응답

`void`가 아닌 모든 메서드에는 직렬화된 최대 응답 크기가 있습니다. 기본값은
4KiB이며, 가변 크기 메서드는 다음과 같이 현실적인 제한을 지정해야 합니다.
`NTL_RPC_BOUNDED_RESPONSE(Bytes, ReturnType)`.

이는 프로토콜 헤더가 아닌 수신 버퍼 계약입니다.

1. 클라이언트는 선언된 용량을 로컬에 할당해 `DeviceIoControl` 출력 버퍼로
   전달합니다.
2. 서버는 콜백을 호출하기 전에 전체 용량을 확인합니다. 따라서 버퍼가 너무 작거나
   계약과 다른 클라이언트가 콜백의 부작용을 일으킨 뒤 응답 쓰기에서 실패하는 일을
   막습니다.
3. 서버는 크기가 제한된 출력 버퍼에 직접 직렬화하고 실제로 쓴 바이트만 반환합니다.
4. 선언된 제한보다 결과가 크면 버퍼 오류로 실패하며, 잘라 내거나 사용하지 않은
   receive buffer 바이트로 완료하지 않습니다.

각 페이로드에 추가 버전/크기 헤더를 넣지 않으며 probe/retry 왕복도 없습니다. 대신
해당 메서드에 지정한 만큼의 receive buffer를 로컬에서 할당합니다. 유효한 응답을
담을 만큼은 크되 신뢰할 수 없는 할당 압력을 제한할 만큼 작은 값을 선택하십시오.

## 요청 및 디코딩 제한

모든 메서드는 직렬화된 요청 바이트와 디코드 중 생성되는 누적 동적 저장소도
제한합니다. 기본값은 요청당 1MiB와 디코드 동적 저장소 4MiB입니다. 가변 크기
입력을 받는 메서드는 계약에 맞는 제한을 지정해야 합니다.

```cpp
constexpr auto update_words =
    ntl::rpc::method<0x901, void(const std::vector<std::string>&)>{}
        .max_request_size<64 * 1024>()
        .max_decode_allocation<256 * 1024>();
```

공유 콜백 매크로에서도 드라이버/클라이언트 단일 소스 선언을 포기하지 않고 같은
계약을 표현할 수 있습니다.

```cpp
NTL_ADD_CALLBACK_ID_1(
    demo_rpc, 0x901,
    NTL_RPC_METHOD_LIMITS(0, 64 * 1024, 256 * 1024, void),
    update_words, const std::vector<std::string>&, words, {
      apply_words(words);
    })
```

세 숫자는 순서대로 응답 용량, 직렬화된 요청 제한, 디코드 할당 예산입니다. `void`
메서드는 응답 용량을 무시하므로 `0`을 사용해 이를 명시하십시오.

요청 제한은 크기가 지나친 `DeviceIoControl` 입력을 디코드하거나 콜백을 호출하기
전에 거부합니다. 작은 페이로드도 매우 큰 컨테이너 원소 수를 선언할 수 있으므로
디코드 할당 제한은 별도로 둡니다. NTL은 중첩 및 연관 컨테이너를 포함한 동적
컨테이너가 사용할 메모리를 할당 전에 이 예산에 반영합니다. 같은 메서드 예산은
클라이언트 측 응답 디코드도 보호합니다.

이는 애플리케이션 유효성 검사가 아니라 전송 리소스 제한입니다. 콜백은 최대 항목
수, 문자열 길이, 허용되는 열거형 값, 요청 작업에 대한 권한 같은 의미 규칙을 계속
검증해야 합니다.

## 타입이 지정된 코어

매크로 프런트엔드는 `ntl::rpc::method<Id, Signature>`와
`ntl::rpc::server::on()`에 위임합니다. 고급 코드와 집중 테스트에서는 이 형식을
직접 사용할 수 있지만, 일반 드라이버/앱 계약에는 같은 소스에서 서버 등록과 사용자
래퍼를 생성하는 매크로를 권장합니다.

## 검증된 페이로드 유형

x64 드라이버/x86 클라이언트 테스트는 사용자/커널 및 프로세스 비트 수 경계를
넘어 다음 페이로드 범주를 검증합니다.

| 카테고리 | 검증된 유형 및 사례 |
| --- | --- |
| 스칼라 | 고정 너비 정수, 부동 소수점 값, `bool` 및 고정 너비 열거형 |
| 텍스트 | `std::string`, `std::wstring`, 포함된 널 및 빈 문자열 |
| 컨테이너 | 시퀀스, 집합, 맵, 비정렬, 중첩 및 고정 크기 컨테이너 |
| 복합 값 | `std::pair`, `std::tuple`, `std::optional` 및 `std::variant` |
| 사용자 형식 | `zpp::serializer` 직렬화를 지원하는 사용자 정의 객체 |
| 경계 사례 | 비어 있거나 큰 페이로드, 너무 작은 응답, 잘린 데이터, 잘못된 길이, 후행 바이트, 요청/디코딩 한도 및 알 수 없는 메서드 ID |

이 표는 실제로 실행한 적용 범위를 기록한 것이며 완전한 허용 목록은 아닙니다.
다른 직렬화 가능 형식도 사용할 수 있지만, 교차 비트 스키마는 아래의 wire
규칙을 따라야 합니다.

## 교차 비트 전송 계약

x86 앱은 x64 NTL RPC 드라이버를 호출할 수 있습니다. wire에 나타나는 크기,
오프셋, 데이터로 표현하는 ID와 핸들에는 `std::uint32_t`, `std::uint64_t` 같은 고정
너비 정수 형식을 사용합니다.

교차 비트 RPC 계약에 `std::size_t`, `std::ptrdiff_t`, `std::uintptr_t`, `ULONG_PTR`,
원시 포인터, 포인터를 직접 포함한 구조체를 넣으면 안 됩니다. x86과 x64에서 인코딩
크기가 다릅니다. 대신 메서드 경계에서 의미 값을 고정 너비 필드로 변환합니다. STL
컨테이너도 원소와 중첩 값 형식이 같은 규칙을 따를 때만 안전합니다. 컨테이너의
프로세스 내부 표현 자체는 전송하지 않습니다.

서버는 애플리케이션 코드를 호출하기 전에 읽지 않은 후행 입력, 불가능한 컨테이너
길이, 요청/디코딩 예산 위반, 너무 작은 출력 버퍼, 알 수 없는 메서드 ID를
거부합니다. 클라이언트는 `DeviceIoControl`이 반환한 바이트 수를 검증하고, 후행
응답 바이트가 있거나 선언된 반환 형식을 만들 수 없는 응답을 거부합니다.

빌드 가능한 x64 드라이버와 x86/x64 클라이언트 테스트는
[`test/rpc/cross-bitness`](../../test/rpc/cross-bitness)를 참조하십시오.
x64 드라이버와 x86 클라이언트의 VM 실행이 기준 교차 비트 사례이며, x64
클라이언트는 동일 비트 비교군입니다.

[`test/rpc/lifecycle-stress`](../../test/rpc/lifecycle-stress)는 수명이 짧은
클라이언트를 반복해서 열고 닫으며, 긴 콜백이 실행 중일 때 드라이버 서비스를
중지했다가 다시 시작한 뒤 새 계약과 호출을 검증합니다. 단일 프로세스 실행으로
드러나지 않는 언로드 및 장치 수명 경쟁 상태를 검사하려면 이 테스트를 Driver
Verifier 아래에서 실행하십시오.

[`test/rpc/async`](../../test/rpc/async)는 시간 초과, 대상 지정 취소, 실행 중 콜백의
협력적 취소, 클라이언트보다 오래 사는 요청 소유권, 동시 overlapped 호출, 보류
리소스 제한, 버려진 요청 정리를 검증합니다.

C++20 클라이언트 테스트는 타입이 지정된 `void` 코루틴 결과, 이미 중지된 토큰과 실행
중 중지되는 토큰, 정확히 한 번만 이루어지는 재개, 중단된 코루틴 프레임의 포기와
소멸, 취소 후 엔드포인트 재사용도 검증합니다.

[`test/rpc/notifications`](../../test/rpc/notifications)는 타입이 지정된 STL 페이로드,
FIFO receive, 시간 초과와 취소, 보류 receive 제한, 핸들 정리, 보류 receive 취소
후 서비스 중지, 언로드와 재시작, 재연결 가능한 세션, ACK까지의 재생, 제한된
backpressure, 영속성 후크 및 x64 드라이버/x86 앱 wire 호환성을 검증합니다.

[`test/rpc/streaming`](../../test/rpc/streaming)은 세션에 바인딩된 타입이 지정된 업로드와
다운로드, 명시적 ACK, 제한된 backpressure와 용량 회복, 시간 초과와 취소, 콜백 취소,
재연결 재생, 최종 레코드, 역순 지속성 복구, 크기 제한 업로드와 일괄 다운로드,
채널 간 우선순위 선택, x64 드라이버/x86 앱 wire 계약을 검증합니다.

[`test/rpc/security`](../../test/rpc/security)는 원래 호출자의 processor mode,
프로세스 ID, impersonation token, 보안 주체 컨텍스트를 검증합니다. 서버를
비동기로 실행해 디스패치가 요청 스레드에서 시스템 작업자로 옮겨진 뒤에도 참조된
호출자 토큰이 유효한지 확인합니다. 허용/거부 보안 설명자, 인증 실패 후 콜백
억제, x64 드라이버/x86 앱 연결 호환성도 검증합니다.

## 신뢰 경계

NTL RPC IOCTL에는 읽기와 쓰기 접근 권한으로 연 핸들이 필요합니다. 기본적으로
`make_server()`는 `IoCreateDeviceSecure()`와 로컬 시스템 및 관리자에게만 접근을
허용하는 ACL을 사용해 명명된 장치를 생성합니다. 교차 비트 테스트는 제한된 가장
토큰으로 엔드포인트를 열 수 없음을 확인합니다.

제품에 다른 주체 집합이 필요하면 `server_options`를 사용합니다. NTL 기본값을
재사용하지 말고 프로젝트 소유 GUID를 장치 클래스에 지정합니다.

```cpp
ntl::rpc::server_options options(L"demo_rpc");
options.security_descriptor(project_sddl, project_device_class_guid);

auto server = ntl::rpc::make_server(driver, options);
server->on(method, callback);
server->start();
```

ACL은 장치를 열 수 있는 주체를 제어합니다. 직렬화 검사와 리소스 제한은 허용된
바이트를 디코딩하는 방식을 제어합니다. 호출자마다 다른 권한이 필요한 경우의
메서드별 권한 부여나 의미 검증을 대신하지는 않습니다.

메서드별 권한 부여가 필요하면 타입이 지정된 메서드를 `on_authorized()`로 등록합니다.
정책은 요청을 역직렬화하거나 애플리케이션 콜백을 실행하기 전에 평가합니다.

```cpp
constexpr ACCESS_MASK read_report = 0x0001;
GENERIC_MAPPING report_mapping{
    read_report, read_report, read_report, read_report};

server->on_authorized(
    read_report_method,
    [&](const ntl::rpc::call_context& call) {
      return call.check_access(report_security_descriptor,
                               read_report,
                               report_mapping);
    },
    [](std::uint32_t report_id) {
      return load_report(report_id);
    });
```

권한 정책은 `NTSTATUS` 또는 `ntl::status`를 반환해야 합니다. 실패 상태는 메서드
인수를 디코드하거나 메서드 콜백을 실행하지 않고 앱에 반환됩니다.
`bool`은 의도적으로 허용하지 않습니다. 그렇지 않으면 C++의 `true`가 0이 아닌
성공 `NTSTATUS` 값으로 해석될 수 있기 때문입니다.
매크로 스키마는 위에서 설명한 동등한
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT[_ID]_*` 형식을 사용하며,
`on_authorized()`와 동일한 실행 순서와 보안 보장을 유지합니다.

`ntl::rpc::call_context`는 `requestor_mode()`, `is_user_mode()`,
`is_kernel_mode()`, `requestor_process_id()`도 제공합니다. 프로세스 ID는 진단용
메타데이터일 뿐입니다. ID는 재사용될 수 있으므로 자격 증명으로 사용하면 안 됩니다.
고급 정책은 `native_subject_context()`를 `SePrivilegeCheck` 같은 문서화된 Windows
보안 API에 전달할 수 있습니다. 이 구조체는 불투명하게 취급하여 멤버를 검사하거나
수정하지 마십시오. 반환 포인터는 비소유이며 해제하거나 보관하면 안 되고, 정책
또는 메서드 콜백 중에만 유효합니다. 일반적인 접근 판단에는 `check_access()`를
권장합니다.

동기 및 비동기 서버 모두 원래 IRP를 처리할 때 subject context를 캡처합니다.
따라서 비동기 디스패치는 시스템 작업자 스레드의 토큰이 아니라 원래 클라이언트의
primary 또는 impersonation token을 평가합니다. subject capture, 권한 검사 및
메서드 콜백에는 `PASSIVE_LEVEL`이 필요합니다.
