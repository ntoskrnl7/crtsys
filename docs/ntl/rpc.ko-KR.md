# NTL RPC

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL RPC는 하나의 공유 매크로 선언에서 `DeviceIoControl` 기반 커널 콜백
디스패처와 사용자 모드 래퍼 함수를 생성합니다. 구현의 기반은 IOCTL ID, 인수 형식,
반환 형식, 직렬화된 최대 응답 크기를 담은 형식화된 메서드 설명자입니다. 직렬화에는
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
됩니다. 앱에서는 같은 선언으로 `demo_rpc::add(left, right)` 같은 형식화된 래퍼를
만들며, 커널 전용 본문은 앱에 컴파일되지 않습니다.

`NTL_ADD_CALLBACK_0`부터 `NTL_ADD_CALLBACK_5`까지의 접미사는 콜백입니다.
인수 개수. 매크로는 이를 사용하여 명명된 매개변수를 생성하고
양쪽에 직렬화 코드가 있습니다. 이러한 기본 양식은
공유 스키마 라인이며 드라이버 및 앱 빌드 시 편리한 선택입니다.
동일한 계약 헤더에서.

메소드 ID가 다음과 같은 경우 `NTL_ADD_CALLBACK_ID_0`~`NTL_ADD_CALLBACK_ID_5`를 사용하세요.
공유 스키마가 다시 형식화되거나 순서가 변경된 후에도 변경되지 않은 상태로 유지됩니다. 이것은
일반 공유 헤더의 필수 형식이 아닌 선택적 ABI 안정성 제어
사용.

장기 실행 콜백은 다음을 통해 `NTL_ADD_CALLBACK_CONTEXT_0`를 사용할 수 있습니다.
`NTL_ADD_CALLBACK_CONTEXT_5` 또는 명시적 ID
`NTL_ADD_CALLBACK_CONTEXT_ID_*` 형태. 콜백 이름 뒤의 인수는 다음과 같습니다.
커널 전용 `ntl::rpc::call_context` 변수 이름:

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

`call`라는 이름을 명시적으로 지정하면 사용 가능한 요청 컨텍스트가
콜백 본문을 사용하고 숨겨진 매크로 식별자에 의존하지 않습니다. 맥락은
직렬화되지 않았으며 사용자 모드 함수 서명의 일부가 아닙니다. 앱
여전히 `demo_rpc::calculate(count)`를 수신하고 있으며
`demo_rpc::calculate_1_method`(동일한 메소드 ID 및 와이어 페이로드 포함)
`NTL_ADD_CALLBACK_ID_1`로부터 수신했을 것입니다.

다음을 통해 `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_0`를 사용하세요.
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_5` 또는 명시적 ID 형식
매크로로 선언된 메서드에도 역직렬화 전에 승인이 필요합니다. 는
콜백 이름 뒤의 인수는 호출 가능한 서버측 정책입니다. 다음
인수는 메서드 콜백에 사용할 수 있는 `call_context`의 이름을 지정합니다.

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

정책 호출 가능 항목은 원래 요청자의 컨텍스트를 수신하고 반환해야 합니다.
`NTSTATUS` 또는 `ntl::status`. 서버 전용 토큰입니다: 클라이언트 확장
이를 폐기하고 동일한 동기식, 비동기식, 중지 토큰을 생성합니다.
코루틴 래퍼는 일반적인 콜백 선언입니다. 실패한 정책 상태
인수 바이트가 디코딩되거나 허용되기 전에 요청을 거부합니다.
메모리를 할당합니다.

명시적 메서드 ID는 엔드포인트 내에서 안정적이고 고유해야 하며
공급업체 IOCTL 기능 범위 `0x800` ~ `0xFFC`; NTL은 다음을 위해 `0xFFD`를 예약합니다.
세션 제어, 알림 수신용 `0xFFE`, 계약용 `0xFFF`
발견. 클라이언트 인수는 다음과 같습니다.
직렬화 전에 선언된 인수 유형으로 변환되므로 고정 너비
메서드 선언은 실수로 기본 너비 호출자 유형을 인코딩할 수 없습니다.

사용자 정의 개체의 경우 `zpp::serializer` 직렬화 기능을 제공합니다. 그만큼`point` 클래스를 테스트했습니다.
[`test/cmake/common/rpc.hpp`](../../test/cmake/common/rpc.hpp)는 다음을 보여줍니다.
패턴.

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

보류 중인 호출 제한 범위는 IRP, 페이징되지 않은 요청 상태 및 대기 상태를 유지합니다.
일. 기본값은 `server_options::default_max_pending_calls`입니다. 선택하다
콜백 비용이 많이 드는 경우 제품별 값이 더 작아집니다. 초과
애플리케이션 콜백이 실행되기 전에 호출이 실패합니다.

사용자 모드 클라이언트는 모든 입력/출력 버퍼, 이벤트 및 `OVERLAPPED`를 소유합니다.
완료될 때까지의 구조:

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
호출자는 계속 기다리거나 `cancel()`을 호출하거나 `async_call`을 파괴할 수
있습니다. 파괴 시 취소를 요청하고 완료 처리가 끝날 때까지 기다린 뒤 버퍼를
해제합니다. `async_call`과 이를 생성한 `client`는 같은 장치 핸들을 각각
유지하므로 `async_call`이 `client`보다 오래 살아도 됩니다.

`CancelIoEx`는 임의 커널 C++ 코드를 강제로 중지할 수 없습니다. 취소하는 경우
대기 중인 콜백이 시작되기 전에 승리하면 NTL은 해당 콜백을 건너뜁니다. 콜백의 경우
이미 실행 중이면 NTL은 반환을 허용하고 출력을 삭제하고
`STATUS_CANCELLED`를 사용한 IRP; 그런 다음 `get()`는 다음과 같이 `std::system_error`를 발생시킵니다.
`ERROR_OPERATION_ABORTED`. 따라서 콜백 코드는 제한된 상태로 유지되어야 하며
취소를 스레드 종료로 처리하면 안 됩니다.

장기 실행 작업을 수행하는 콜백은 협력을 선택할 수 있습니다.
선언된 RPC 이전에 `ntl::rpc::call_context`를 수락하여 취소
인수:

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

동기식 `invoke()`는 일반 및 비동기식 모두에서 계속 사용 가능합니다.
끝점. 비동기 끝점에서는 보류 중인 작업을 기다리고
기존 유형 반환 API를 유지합니다.

### C++20 중지 토큰 및 코루틴

기존 `invoke_async(method, args...)` 오버로드는 계속해서 사용할 수 있습니다.
C++14 이상. C++20 클라이언트는 동일한 작업을 중지 토큰에 바인딩할 수 있습니다.

```cpp
#include <stop_token>

std::stop_source source;
auto call = demo_rpc::read_values_async(source.get_token(),
                                        std::uint32_t{16});

source.request_stop();
```

반환된 `async_call<T>`는 `std::stop_callback` 등록을 소유합니다. 중지 요청은
해당 작업에 대해서만 `CancelIoEx`를 호출합니다. 파괴 시 네이티브 작업 상태가
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
awaiter가 아직 보류 중일 때 코루틴을 파괴하면 이후 재개를 막고 해당 요청을
취소합니다. 다른 코루틴 awaitable과 마찬가지로 외부 작업 소유자는 자신이
조정하지 않는 재개와 동시에 같은 코루틴 프레임을 파괴하면 안 됩니다.

`receive_async()`에도 같은 C++20 중지 토큰 오버로드를 사용할 수 있습니다.
C++14 및 C++17 번역 단위는 `<stop_token>`이나 `<coroutine>`을 포함하지 않고
기존 API와 ABI 인터페이스를 유지합니다. 내부 비동기 작업의 레이아웃도 모든 언어
모드에서 같으므로, 한 실행 파일에서 C++14 번역 단위와 중지 토큰 또는 코루틴
어댑터를 사용하는 C++20 번역 단위를 안전하게 링크할 수 있습니다.

## 커널-앱 알림

알림 채널은 공유되는 직렬화 가능한 페이로드 하나를 설명합니다.
드라이버와 앱. 해당 ID는 네이티브가 아닌 고정 너비 애플리케이션 채널 ID입니다.
포인터 또는 아키텍처 크기 값:

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

`start()` 이전에 모든 채널을 등록하세요. 게시하려면 `PASSIVE_LEVEL`가 필요합니다.
임의의 페이로드 직렬화는 CRT/STL 코드를 실행할 수 있기 때문입니다.

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

`receive(progress)`는 차단 형식을 제공합니다. `receive_async(progress)`
동일한 `ready()`를 갖는 `notification_wait<payload_type>`를 반환합니다.
`wait_for()`, `wait()`, `cancel()` 및 `get()` 소유권 규칙은 다음과 같습니다.
`async_call<T>`.

전송은 역호출 큐 방식입니다. 앱의 각 receive는 보류 `METHOD_BUFFERED` IRP 하나를
제공하고, 성공한 `try_notify()`는 일치하는 채널에서 가장 오래 기다린 receive 하나를
꺼냅니다. 의도적으로 큐 전달이며 broadcast 전달이 아닙니다. 따라서 독립 소비자가
여럿이면 같은 엔드포인트의 이벤트를 서로 경쟁해 받습니다. broadcast 의미가 필요한
제품은 엔드포인트를 분리하거나 애플리케이션 계약에 구독자 ID를 구현해야 합니다.

NTL은 취소 경쟁을 처리하기 위해 `IO_CSQ`를 사용합니다. `CancelIoEx`,
`notification_wait` 파괴, `IRP_MJ_CLEANUP`, 서버 종료는 각각 receive 하나를 정확히
한 번 큐에서 제거해 완료합니다. `stop()`은 새 receive를 먼저 거부하고 큐에 있는
모든 receive를 실패로 완료한 뒤 일반 RPC 콜백 rundown을 기다립니다.

레거시 WDM 드라이버는 앱이 여전히 서비스를 소유하고 있는 동안 서비스 중지를 완료할 수 없습니다.
장치 핸들을 엽니다. `SERVICE_STOPPED`를 기다리기 전에 모두 취소하거나 파기하세요.
`notification_wait` 개체를 만들고 해당 클라이언트를 닫습니다. 그런 다음 `IRP_MJ_CLEANUP`
앱의 대기 중인 수신을 제거하여 언로드가 시작되도록 합니다. 서버 종료
플러시는 엔드포인트가 여전히 소유하고 있는 모든 수신에 대한 최종 방어로 남아 있습니다.

보류 중인 수신 제한 범위에는 IRP 및 해당 I/O 관리자 버퍼가 유지됩니다.
수신이 없으면 이벤트가 유지되지 않습니다. `try_notify()`가 반환됩니다.
`STATUS_NOT_FOUND`를 사용하면 드라이버가 삭제, 계산, 병합 또는 대기열을 만들 수 있습니다.
제품별 정책을 활용한 이벤트입니다. 페이로드 바이트에는 추가 프레임워크가 포함되어 있지 않습니다.
헤더. 수신 요청에는 숨겨진 `std::uint32_t` 채널 ID가 하나만 전달됩니다.
따라서 동일한 x64 드라이버 계약을 x86 및 x64 앱에서 사용할 수 있습니다.

## 클라이언트 세션 및 안정적인 알림

위의 알림 API는 의도적으로 일시적입니다. 앱이 수신을 대기열에 넣습니다.
먼저 드라이버는 수신이 없을 때 이벤트 자체를 삭제하거나 처리합니다.
그 때까지 이벤트를 계속 사용할 수 있어야 하는 경우 옵트인 클라이언트 세션을 사용하세요.
특정 클라이언트가 이를 인정합니다.

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

`client_session::state()`는 핸들 전반에 걸쳐 애플리케이션 소유 유형 상태를 저장합니다.
연결을 끊었다가 다시 연결하세요. 세션 후크는 `PASSIVE_LEVEL`에서 실행됩니다. 는
열기 및 재개 정책에 제공된 `call_context`는 현재
요청자이므로 토큰을 신뢰하는 대신 재연결 시 신원을 다시 확인할 수 있습니다.
유일한 승인 결정. `client_session::token()`는 애플리케이션을 허용합니다.
후크는 자체 외부 인증 또는 구독 메타데이터를 복원합니다. 치료하다
해당 토큰을 비밀로 설정하고 절대 인쇄하지 마세요. 콜백은
해당 콜백 기간 동안만 `client_session` 포인터입니다. 그러면 안 된다
포인터를 유지하십시오. 활성 RPC에 대한 명시적인 종료 및 보존 만료 대기
`on_session_close()`가 실행되기 전 세션과 관련된 콜백.

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

- `try_notify(session_id, ...)`는 한 레코드에 대해서만 하나의 레코드를 직렬화하고 대기열에 넣습니다.
  세션을 구독했습니다. `PASSIVE_LEVEL`가 필요합니다.
- `receive_reliable()` 및 `receive_reliable_async()` 반환
  `notification_delivery<T>`, 형식화된 페이로드와 0이 아닌 페이로드가 포함되어 있습니다.
  순서.
- 전달된 레코드는 `acknowledge()`가 성공할 때까지 세션에 유지됩니다.
  ACK가 비행 중 상태를 재설정하기 전에 연결을 끊으면 다음 처리
  토큰은 동일한 시퀀스를 다시 수신합니다.
- 장치 핸들을 파괴하거나 닫으면 세션 연결이 끊어집니다. 그렇지 않다
  재생 상태를 삭제합니다. `session.token`를 저장하고 `resume_session(token)`로 호출하세요.
  새로 열린 클라이언트에서.
- `close_session()`는 세션과 해당 지속 레코드를 명시적으로 삭제합니다.
  해당 토큰은 나중에 재개될 수 없습니다.
- `unsubscribe()`는 보류 중인 안정적인 수신을 취소합니다. 탈퇴를 거부합니다
  해당 채널에는 아직 확인되지 않은 기록이 있으므로 무음 데이터가 방지됩니다.
  손실; 먼저 해당 레코드를 ACK하거나 처리하세요.
- 세션당 및 엔드포인트 전체 대기열 제한은 다음과 같이 구성됩니다.
  `max_reliable_notifications_per_session()` 및
  `max_reliable_notifications()`. 가득 찬 대기열은 `STATUS_DEVICE_BUSY`를 반환합니다.
  무제한 커널 할당이 아닌 제한된 배압을 제공합니다.

재연결 토큰은 임의의 128비트 기능입니다. 비밀로 취급하세요.
이를 기록하거나 관련 없는 프로세스에 노출하지 마세요. 엔드포인트 ACL 및
`on_session_resume()`는 정책 경계를 유지합니다.

인메모리 세션은 연결이 끊어진 동안 `session_retention_ms()` 후에 만료됩니다.
NTL은 선택적으로 다음을 통해 더 긴 수명을 연결할 수 있습니다.
`notification_storage(std::shared_ptr<notification_store>)`. 매장에서 받아요
게시 전 직렬화된 레코드, ACK 제거, 토큰 복원 및
명시적 세션 삭제. 저장소가 설치되지 않으면 저장소 I/O가 발생하지 않습니다.
저장소 후크는 내부 세션 잠금을 유지하지 않고 `PASSIVE_LEVEL`에서 실행됩니다.
연결 끊기, 재시도 및 종료가 경쟁할 수 있으므로 멱등성이 있어야 합니다.
외부 저장소 오류. 보존 기간이 만료되면 메모리 내 복사본만 해제됩니다.
`erase_session()`를 호출하지 않습니다. 따라서 외부 저장소는 복원할 수 있습니다.
나중에 확인되지 않은 레코드가 하나 이상 유지되면 토큰이 삭제됩니다.

레지스트리 지원 복구의 경우 다음을 포함합니다.
`<ntl/rpc/registry_notification_store>` 및 제공된 항목을 명시적으로 설치합니다.
어댑터:

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

세션당 하나의 제한된 `REG_BINARY` 값을 유지하고 레코드를 제거합니다.
ACK 이후. 호출자는 키, ACL, 휘발성 또는 비휘발성 수명을 선택합니다.
그리고 청소 정책.
복원된 기록은 어떤 순서로도 제공될 수 있습니다. NTL은 원본을 기준으로 정렬합니다.
세션 시퀀스 및 중복 시퀀스 거부, 일반 터미널 마커
해당 스트림 터미널 이후에 나타나는 알림 및 스트림 기록기록. 따라서 저장소 구현은 실수로 다시 열리거나
일관되지 않은 데이터를 반환하여 완료된 스트림에 추가합니다.

일시적인 `receive()`/`try_notify()` 및 세션 기반의 안정적인 전달은
동일한 유형의 채널에 대한 별도의 모드. 기존 임시 알림 코드
세션을 생성하거나 암시적으로 버퍼링을 얻지 않습니다.

## 형식화된 스트리밍

NTL RPC 스트림은 하나의 세션 바인딩된 양방향 유형 채널입니다. 둘
방향은 독립적입니다.

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

앱은 업로드를 작성하는 동안 `read_async()`를 보류 상태로 유지할 수 있으므로 업로드하고
다운로드는 동시에 진행될 수 있습니다. 읽지 않아도 다운로드가 사라지지 않습니다.
현재 보류 중: 앱이 실행될 때까지 신뢰할 수 있는 세션 대기열에 남아 있습니다.
이를 읽고 명시적으로 ACK합니다. `complete()` 및 `fail()`도 대기 중인 레코드입니다.
암시적인 닫기 작업이 아닙니다.

아래 `upload_chunk::finish`는 NTL 전송 플래그가 아닌 애플리케이션 데이터입니다.
앱은 어떤 청크가 마지막 입력인지 알고 호출하기 전에 필드를 설정합니다.
`write()`. 이 샘플 프로토콜을 사용하면 드라이버가 그 후에 출력을 완료할 수 있습니다.
마지막 입력. 수명이 긴 스트림은 이 필드를 생략하고 다음에 따라 끝날 수 있습니다.
대신 드라이버 소유 상태입니다.

공유 계약에서 직렬화된 유형과 스트림 콜백을 한 번 정의합니다.
헤더:

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

앱은 세션을 시작하고 생성된 스트림 퍼사드를 연 뒤 쓰기 전에 읽기를
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

모든 업로드에 승인이 필요한 경우 `NTL_ADD_AUTHORIZED_STREAM_ID`를 사용하세요.
역직렬화 전 정책. 동일한 캡처된 요청자 보안이 있습니다.
`on_authorized()`와 같은 의미. 생성된 앱 파사드는 변경되지 않습니다.

직접 `ntl::rpc::stream`, `server::on_stream()` 및 `client::open_stream()`
계약에 사용자 지정 업로드, 다운로드 또는 디코딩이 필요한 경우 API를 계속 사용할 수 있습니다.
제한이 있거나, 공유 매크로 헤더에 등록을 표현할 수 없는 경우입니다.

`write_async()` 및 `read_async()`는 동일한 소유 비동기 작업을 반환합니다.
일반 RPC 및 알림 호출에 사용되는 유형입니다. 그들의 `wait_for()`,
따라서 `cancel()`, C++20 중지 토큰 및 코루틴 규칙은 변경되지 않고 유지됩니다.
시간 초과로 인해 기본 I/O 요청이 활성 상태로 유지됩니다. 계속 기다리세요. 취소하세요.
아니면 그 주인을 파멸시키거나. 이전에 처리되지 않은 읽기 및 쓰기를 취소하거나 비우기
`close()` 또는 장치 핸들을 닫습니다. `client_stream` 소멸자는 그렇지 않습니다.
레코드가 ACK되지 않은 상태에서 닫기가 실패할 수 있으므로 스트림을 자동으로 닫습니다.

드라이버 출력은 안정적인 알림 대기열을 사용합니다. 모든 성공적인 읽기는 다음을 수행해야 합니다.
터미널 레코드를 포함하여 ACK를 받습니다. `try_complete()`는 성공적인 종료를 대기열에 넣습니다.
스트림; `try_fail()`는 WDK를 전달하는 실패한 터미널 레코드를 대기열에 넣습니다.
`NTSTATUS`. ACK 전에 연결을 끊으면 이후에 동일한 시퀀스가 재생됩니다.
`resume_session()`.

터미널 레코드가 대기열에 추가된 후 NTL은 이후 업로드, 출력 청크,
해당 세션 및 스트림에 대한 터미널 레코드를 복제합니다. 클라이언트 이후
터미널에 ACK를 보내고 `STATUS_END_OF_FILE`로 인해 또 다른 읽기가 실패합니다. 클라이언트그런 다음 `close()`를 호출합니다. 닫으면 해당 스트림 인스턴스가 제거되므로 동일한 세션이
나중에 새로운 인스턴스를 열 수 있습니다. 터미널 ACK가 유지되기 전에 연결 끊기
재연결 재생을 위한 터미널 시퀀스와 쓰기 불가 상태 모두.

한 생산자가 연속적으로 건 호출은 해당 순서대로 대기됩니다. 여러 개라면
콜백은 동일한 세션과 스트림에 동시에 게시됩니다.
병합 순서를 정의하는 애플리케이션 시퀀스 필드입니다. 터미널이 기다리고 있다
이미 시작된 출력 게시 뒤에 새 게시가 방지됩니다.
시작.

배압은 양방향으로 제한됩니다.

- 다운로드는 구성된 세션별 및 엔드포인트 신뢰할 수 있는 대기열을 사용합니다.
  한계. `try_write()`, `try_complete()` 및 `try_fail()` 반환
  대기열이 가득 찬 동안 `STATUS_DEVICE_BUSY`. ACK는 용량을 해제합니다. 에이
  실패한 터미널 대기열에 예약이 롤백되므로 스트림은 그대로 유지됩니다.
  쓰기 가능하며 생산자는 용량이 해제된 후 다시 시도할 수 있습니다.
- 업로드는 일반적인 제한된 RPC 요청입니다. 직렬화된 요청 및 디코딩
  예산은 스트림 설명자에서 나오는 반면 비동기 동시성은
  `server_options::max_pending_calls()`로 제한됩니다.

### 제한된 배치 및 전달 우선순위

단일 레코드 `write()`, `read()`, `read_async()` 및 `acknowledge()` API
기본값으로 유지됩니다. 스트림은 여러 직렬화된 레코드를 다음을 통해 이동할 수도 있습니다.
요소 유형을 변경하지 않고 하나의 IOCTL:

```cpp
std::vector<upload_chunk> uploads{first, second, last};
records.write_batch(uploads);

auto downloads = records.read_batch(4);
for (const auto& delivery : downloads.values()) {
  consume(delivery.payload());
}
records.acknowledge(downloads);
```

`write_batch()`는 요소당 한 번씩 등록된 업로드 콜백을 호출합니다.
벡터 순서를 확인하고 요소 간 협력 취소를 확인합니다. 그렇지 않다
트랜잭션: 이후 콜백이 실패하면 이전 콜백의 효과는 다음과 같습니다.
롤백되지 않았습니다. 빈 배치 및 설명자보다 큰 배치
`max_batch_records()`가 거부되었습니다. 직렬화된 요청과 누적
디코드 할당은 스트림의 일반 업로드 제한에 맞아야 합니다.

`read_batch()`는 이미 요청된 레코드 수까지 반환합니다.
준비. 배치가 채워질 때까지 기다리지 않습니다. 반환된 모든 기록에는
자체 시퀀스이며 ACK까지 재생 가능한 상태로 유지됩니다. 일괄 ACK 편의성
함수는 해당 ACK를 순서대로 보냅니다. 마찬가지로 원자적이지 않으므로 오류가 발생합니다.
이전 ACK를 실행 취소하지 마세요. 시간 초과, `CancelIoEx`, 중지 토큰, 다시 연결, 터미널,
지속성 동작은 신뢰할 수 있는 단일 읽기와 동일합니다.

컴파일 시간 제한의 기본값은 16이며 64를 초과할 수 없습니다.

```cpp
constexpr auto records =
    ntl::rpc::stream<0x920, upload_chunk, download_chunk>{}
        .with_batch_records<8>()
        .with_priority<ntl::rpc::delivery_priority::high>();
```

하나의 임의 채널 배치 수신이 레코드 중에서 선택되는 경우에만 우선순위가 중요합니다.
여러 개의 신뢰할 수 있는 구독 채널에서 준비되어 있습니다. 채널별
`read()` 또는 `read_batch()`는 이미 채널 이름을 지정했으므로 아무것도 없습니다.
우선순위를 정합니다. 한 채널 내에서 NTL은 항상 시퀀스/FIFO 순서를 유지합니다.
모든 채널 소비자의 경우:

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

선택기는 `critical`, `high`, `normal`, `background`를 비교합니다. 기록
동일한 우선순위를 가진 세션은 세션 순서에 따라 선택됩니다. 보류 중인 배치
하나 이상의 레코드가 준비되는 즉시 완료되므로 낮은 레코드가 지연되지 않습니다.
미래의 높은 우선순위 레코드가 도착할 수 있기를 바라는 마음으로 우선순위 레코드를 작성합니다.
`max_records` 및 `max_bytes` 인수는 커널과 앱 메모리를 하나로 바인딩했습니다.
수신.

이 스트림은 `METHOD_BUFFERED`를 통해 직렬화된 청크를 전송합니다. 그렇지 않다
제로 복사 데이터 경로. 공유 메모리 또는 MDL 지원 전송이 다릅니다.
매핑, 프로세스 종료, 취소 및 드라이버 언로드 소유권 요구 사항따라서 보이지 않는 데이터 플레인이 아닌 명시적인 선택적 데이터 플레인입니다.
이 직렬화된 스트림 계약으로 변경하세요.

## 공유 메모리 데이터 플레인

대용량 고정 레이아웃 레코드의 경우 옵트인 클라이언트 세션이 등록할 수 있습니다.
`client::register_shared_region()`를 사용한 호출자 소유 메모리. 드라이버가 조사하고,
고정하고 원래 요청자 프로세스 컨텍스트의 페이지를 매핑하고
고정 너비 `region_handle`. RPC 방법은 `buffer_token` 값만 교환합니다.
콜백은 명시적으로 `call_context::try_resolve()`를 통해 이를 해결합니다.
드라이버 읽기 또는 드라이버 쓰기 액세스.

일반적인 `ntl::ipc::shared_ring<T, Capacity>` 레이아웃은 제한된 SPSC를 제공합니다.
`DeviceIoControl`를 통해 각 레코드를 직렬화하지 않고 배압을 수행합니다. 하나를 사용
이중 트래픽의 경우 방향별로 링이 울립니다. 레코드 필드는 고정 너비여야 하며
포인터, 핸들, `size_t`, 문자열 또는 STL 컨테이너를 포함해서는 안 됩니다.

공유 영역에는 클라이언트 세션이 필요합니다. 등록 취소 시 무효화되며,
연결 끊기, 세션 닫기, 보존 만료 및 서버 중지. 세션당 수
바이트 할당량은 `server_options`를 통해 구성됩니다. 참조
API 및 수명 규칙의 경우 [`IPC shared memory`](./ipc.ko-KR.md)
x64 드라이버용 [`test/rpc/cross-bitness`](../../test/rpc/cross-bitness)
x64/x86-클라이언트 VM 적용 범위.

## 계약 확인

앱은 엔드포인트를 연 후 한 번 공유 계약의 유효성을 검사할 수 있습니다.
계약 검색은 모든 RPC에 헤더가 추가되는 것이 아니라 예약된 쿼리입니다.
요청:

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

void가 아닌 모든 메서드에는 직렬화된 최대 응답 크기가 있습니다. 기본값은
4KiB; 가변 크기 방법은 다음과 같이 현실적인 한계를 설정해야 합니다.
`NTL_RPC_BOUNDED_RESPONSE(Bytes, ReturnType)`.

이는 프로토콜 헤더가 아닌 수신 버퍼 계약입니다.

1. 클라이언트는 선언된 용량을 로컬로 할당하고 이를
   `DeviceIoControl` 출력 버퍼.
2. 서버는 콜백을 호출하기 전에 전체 용량을 확인합니다. 에이
   너무 작거나 일치하지 않는 클라이언트로 인해 콜백 측을 트리거할 수 없습니다.
   효과가 나타나고 답글을 작성하는 동안 실패합니다.
3. 서버는 제한된 출력 버퍼로 직접 직렬화하고 반환합니다.
   실제로 쓰여진 바이트만.
4. 선언된 제한보다 큰 결과는 버퍼 오류로 인해 실패합니다. 그것은이다
   사용되지 않은 수신 버퍼 바이트는 절대로 자르거나 완료되지 않습니다.

모든 페이로드에는 추가 버전이나 크기 헤더가 없으며 프로브/재시도도 없습니다.
왕복. 비용은 해당 항목에 대해 선택된 로컬 수신 버퍼 할당입니다.
방법. 유효한 응답을 위해서는 충분히 큰 제한을 유지하되 제한할 수 있을 만큼 작게 유지하십시오.
신뢰할 수 없는 할당 압력.

## 요청 및 디코딩 제한

모든 방법은 또한 직렬화된 요청 바이트와 누적 동적을 제한합니다.
디코딩하는 동안 생성된 저장소입니다. 기본값은 요청당 1MiB, 4MiB입니다.
디코딩된 동적 스토리지. 가변 크기 입력을 허용하는 메서드는 다음과 같이 설정되어야 합니다.
계약에 적합한 한도:

```cpp
constexpr auto update_words =
    ntl::rpc::method<0x901, void(const std::vector<std::string>&)>{}
        .max_request_size<64 * 1024>()
        .max_decode_allocation<256 * 1024>();
```

공유 콜백 매크로는 다음을 포기하지 않고 동일한 계약을 노출합니다.
단일 소스 드라이버/클라이언트 선언:

```cpp
NTL_ADD_CALLBACK_ID_1(
    demo_rpc, 0x901,
    NTL_RPC_METHOD_LIMITS(0, 64 * 1024, 256 * 1024, void),
    update_words, const std::vector<std::string>&, words, {
      apply_words(words);
    })
```

세 가지 숫자 값은 응답 용량, 직렬화된 요청 제한 및
디코드-할당 예산을 순서대로 지정합니다. `void` 메서드는 응답을 무시합니다.
용량; 이를 명시적으로 나타내려면 `0`를 사용하세요.

요청 제한은 디코딩 전에 크기가 큰 `DeviceIoControl` 입력을 거부합니다.
또는 콜백을 호출합니다. 디코드 할당 제한은 다음과 같은 이유로 별개입니다.
작은 페이로드는 매우 큰 컨테이너 수를 선언할 수 있습니다. NTL은 동적 요금을 청구합니다.
이 예산에 대한 중첩 및 연관 컨테이너를 포함한 컨테이너
할당 전. 동일한 방법의 예산으로 클라이언트측 응답을 보호합니다.
디코딩.

이는 애플리케이션 유효성 검사가 아니라 전송 리소스 제한입니다. 콜백은 최대 항목
수, 문자열 길이, 허용되는 열거형 값, 요청 작업에 대한 권한 같은 의미 규칙을 계속
검증해야 합니다.

## 유형화된 코어

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
협력적 취소, 클라이언트보다 오래 사는 요청 소유권, 동시에 중복된 호출, 보류
리소스 제한, 버려진 요청 정리를 검증합니다.

C++20 클라이언트 테스트는 형식화된 `void` 코루틴 결과, 이미 중지된 토큰과 실행
중 중지되는 토큰, 정확히 한 번만 이루어지는 재개, 중단된 코루틴 프레임의 포기와
파괴, 취소 후 엔드포인트 재사용도 검증합니다.

[`test/rpc/notifications`](../../test/rpc/notifications)는 STL 형식을 커버합니다.
페이로드, FIFO 수신, 시간 초과 및 취소, 보류 중인 수신 제한,
정리 처리, 보류 중인 수신 취소 후 서비스 중지, 언로드,
재시작, 세션 재연결, ACK까지 재생, 제한된 역압,
지속성 후크 및 x64-driver/x86-app 와이어 호환성.

[`test/rpc/streaming`](../../test/rpc/streaming)은 세션에 바인딩된 형식화된 업로드와
다운로드, 명시적 ACK, 제한된 역압과 용량 회복, 시간 초과와 취소, 콜백 취소,
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

메서드별 권한 부여가 필요하면 형식화된 메서드를 `on_authorized()`로 등록합니다.
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

승인 정책은 `NTSTATUS` 또는 `ntl::status`를 반환해야 합니다. 실패했습니다
메서드 인수를 디코딩하거나 메서드 콜백을 실행하지 않고 상태를 앱에 반환합니다.
`bool`은 의도적으로 허용하지 않습니다. 그렇지 않으면 C++의 `true`가 0이 아닌
성공 `NTSTATUS` 값으로 해석될 수 있기 때문입니다.
매크로 스키마는 위에서 설명한 동등한
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT[_ID]_*` 형식을 사용하며,
`on_authorized()`와 동일한 실행 순서와 보안 보장을 유지합니다.

`ntl::rpc::call_context`는 또한 `requestor_mode()`, `is_user_mode()`,
`is_kernel_mode()` 및 `requestor_process_id()`. 프로세스 ID는 진단입니다.
메타데이터 전용: ID는 재사용할 수 있으며 자격 증명으로 사용하면 안 됩니다. 고급
정책은 문서화된 Windows 보안에 `native_subject_context()`를 전달할 수 있습니다.
`SePrivilegeCheck`와 같은 API. 구조를 불투명하게 처리: 검사하지 않음
또는 해당 멤버를 수정합니다. 반환된 포인터는 소유하지 않으며 해제되어서는 안 됩니다.
또는 유지되며 정책 또는 메서드 콜백 중에만 유효합니다. 선호
일반적인 액세스 결정을 위한 `check_access()`.

동기 및 비동기 서버 모두 주제 컨텍스트를 캡처하는 동시에
원래 IRP를 처리합니다. 따라서 비동기 디스패치는 다음을 평가합니다.
시스템 작업자가 아닌 원래 클라이언트의 기본 또는 가장 토큰
스레드의 토큰. 주제 캡처, 승인 및 메서드 콜백에는 다음이 필요합니다.
`PASSIVE_LEVEL`.
