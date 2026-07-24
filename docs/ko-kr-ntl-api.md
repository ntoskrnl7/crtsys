# NTL API 문서

[한국어 문서로 돌아가기](./ko-kr.md)

NTL은 `crtsys`가 함께 제공하는 작은 C++ helper 레이어입니다. WDK 객체를
숨기기보다는, 드라이버 코드에서 RAII ownership, callback 등록, user-mode와
kernel-mode 사이의 간단한 RPC를 쓰기 쉽게 만드는 것을 목표로 합니다.

헤더는 [`include/ntl`](../include/ntl)에 있습니다.

## IRQL 계약 표기

NTL은 주로 드라이버 제어 경로를 대상으로 설계되었습니다. API가 더 넓은
계약을 문서화하지 않는다면 기본값은 `PASSIVE_LEVEL`로 보아야 합니다.

아래 API 설명은 보수적인 계약을 사용합니다.

- `PASSIVE_LEVEL`은 helper가 allocation, wait, throw, pageable code 접근,
  runtime/STL state 의존성을 가질 수 있음을 의미합니다.
- `<= APC_LEVEL`은 underlying WDK primitive가 APC-level caller를 허용할 수
  있지만, 여전히 DPC/ISR-safe는 아니라는 뜻입니다.
- `<= DISPATCH_LEVEL` 또는 DPC 전용 계약은 해당 문맥을 의도하고 작성한 API에만
  명시합니다.

프로젝트 수준의 실행 모델은 [설계 근거와 운영 경계](./ko-kr-design-rationale.md)를
참고하세요.

## 진입점

`CRTSYS_NTL_MAIN`을 켜면 다음 함수를 구현합니다.

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys`는 WDK driver entry point를 이 함수로 연결합니다. wrapper는
`ntl::main` 호출 전에 stack expansion helper도 사용합니다.

IRQL: `PASSIVE_LEVEL`.

## `ntl::status`

헤더: [`include/ntl/status`](../include/ntl/status)

`NTSTATUS`를 감싸는 작은 wrapper입니다.

- `status(NTSTATUS status)`
- `bool is_ok() const`
- `bool is_info() const`
- `bool is_warn() const`
- `bool is_err() const`
- `operator NTSTATUS() const`
- `static status ok()`

IRQL: 값 타입 연산 자체는 allocation이나 wait를 하지 않습니다. 다만 해당
status 흐름이 현재 IRQL에서 유효한지는 surrounding WDK call path가 결정합니다.

## Exceptions

헤더: [`include/ntl/except`](../include/ntl/except)

- `ntl::exception`
  - `ntl::status`를 보관합니다.
  - 설명 메시지를 함께 가집니다.
- `ntl::seh::try_except(fn, args...)`
  - MSVC SEH `__try` / `__except` 안에서 callable을 실행합니다.
  - 성공하면 `{true, 0}`, SEH exception을 잡으면 `GetExceptionCode()`에서 온
    `{false, exception_code}`를 반환합니다.

IRQL: C++ exception path는 `PASSIVE_LEVEL` 전용으로 취급하세요. WDK callback
boundary, spin-lock-held region, DPC, ISR, paging I/O 경로를 exception이
넘어가게 하지 마세요. 그런 동작이 필요하다면 별도 테스트와 문서화가 먼저
필요합니다.

## Stack Expansion

헤더: [`include/ntl/expand_stack`](../include/ntl/expand_stack)

기본 커널 스택보다 더 많은 스택이 필요한 경로에서 `ntl::expand_stack`을
사용합니다.

```cpp
auto opts = ntl::expand_stack_options().wait(true);
auto result = ntl::expand_stack(std::move(opts), [] {
  return do_expensive_work();
});
```

API:

- `ntl::expand_stack_size_max`
- `ntl::expand_stack_options`
  - `stack_size(size_t)`
  - `wait(bool)`
  - `ignore_failure(bool)`
- `ntl::expand_stack(options, fn, args...)`

IRQL: `crtsys`에서 문서화한 사용 문맥은 `PASSIVE_LEVEL`입니다. wrapper는 C++
runtime path를 사용하고 실패 시 throw할 수 있습니다. hot path escape hatch가
아니라 control-path helper로 보아야 합니다.

## Driver Object

헤더: [`include/ntl/driver`](../include/ntl/driver)

`ntl::driver`는 `DRIVER_OBJECT`를 감쌉니다.

- `create_device<Extension>(device_options&)`
  - `ntl::device<Extension>`을 생성합니다.
  - device extension 영역에 extension 객체를 생성합니다.
- `on_unload(callback)`
  - C++ unload callback을 등록합니다.
- `name() const`
  - driver name을 `std::wstring`으로 반환합니다.

IRQL: `PASSIVE_LEVEL`. C++ object와 container를 사용하며, driver initialization,
unload registration, setup path를 위한 helper입니다.

## IRP View

헤더: [`include/ntl/irp`](../include/ntl/irp)

`ntl::irp`는 dispatch 중 전달된 `PIRP`를 감싸는 non-owning view입니다. IRP를
complete하거나 reference하거나 보관하지 않습니다.

- `get() const`
  - raw `PIRP`를 반환합니다.
- `operator->() const`
  - raw `PIRP`를 반환합니다.
- `stack_location() const`
  - `IoGetCurrentIrpStackLocation()` 결과를 반환합니다.
- `major_function() const`
  - 현재 major function을 반환합니다.
- `status() const` / `status(NTSTATUS)`
  - `IoStatus.Status`를 읽거나 씁니다.
- `information() const` / `information(ULONG_PTR)`
  - `IoStatus.Information`을 읽거나 씁니다.

IRQL: IRP를 전달한 dispatch routine의 문맥을 따릅니다.

## Device Object

헤더: [`include/ntl/device`](../include/ntl/device)

`ntl::device_options`는 device 생성 옵션을 구성합니다.

- `name(std::wstring)`
- `type(DEVICE_TYPE)`
- `exclusive(bool = true)`
- `name() const`
- `type() const`
- `is_exclusive() const`

`ntl::device<Extension>`은 `PDEVICE_OBJECT`를 소유합니다.

- `extension()`
  - typed extension 객체를 반환합니다.
- `on_create(callback)`
  - `IRP_MJ_CREATE` handler를 등록합니다.
  - callback signature: `void(ntl::irp&)`
- `on_close(callback)`
  - `IRP_MJ_CLOSE` handler를 등록합니다.
  - callback signature: `void(ntl::irp&)`
- `on_device_control(callback)`
  - `IRP_MJ_DEVICE_CONTROL` handler를 등록합니다.
- `name() const`
- `type() const`
- `detach()`
  - raw device object ownership을 해제합니다.

Device control helper type:

- `ntl::device_control::code`
- `ntl::device_control::in_buffer`
- `ntl::device_control::out_buffer`
- `ntl::device_control::dispatch_fn`

IRQL: 특정 dispatch path가 따로 audit되고 문서화되지 않았다면
`PASSIVE_LEVEL`로 취급하세요. wrapper는 C++ callback과 ownership helper를
사용합니다.

## RPC

헤더:

- [`include/ntl/rpc/common`](../include/ntl/rpc/common)
- [`include/ntl/rpc/server`](../include/ntl/rpc/server)
- [`include/ntl/rpc/client`](../include/ntl/rpc/client)
- [`include/ntl/rpc/registry_notification_store`](../include/ntl/rpc/registry_notification_store)

RPC helper는 하나의 공유 선언에서 `DeviceIoControl` 기반 kernel callback
dispatcher와 user-mode wrapper를 생성합니다. 직렬화는 `zpp::serializer`를
사용합니다.

주요 macro:

- `NTL_RPC_BEGIN(name)`
- `NTL_RPC_END(name)`
- `NTL_ADD_CALLBACK_0`
- `NTL_ADD_CALLBACK_1`
- `NTL_ADD_CALLBACK_2`
- `NTL_ADD_CALLBACK_3`
- `NTL_ADD_CALLBACK_4`
- `NTL_ADD_CALLBACK_5`
- `NTL_ADD_CALLBACK_ID_0`
- `NTL_ADD_CALLBACK_ID_1`
- `NTL_ADD_CALLBACK_ID_2`
- `NTL_ADD_CALLBACK_ID_3`
- `NTL_ADD_CALLBACK_ID_4`
- `NTL_ADD_CALLBACK_ID_5`
- `NTL_ADD_CALLBACK_CONTEXT_N` / `NTL_ADD_CALLBACK_CONTEXT_ID_N`
- `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_N` /
  `NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_N`

suffix의 `0`부터 `5`는 callback 인자 개수입니다. 드라이버와 앱이 같은 공유
schema를 사용하는 일반적인 경우에는 간결한 `NTL_ADD_CALLBACK_N`을
사용하세요. schema 재배치 뒤에도 같은 method ID를 유지해야 할 때는 vendor
IOCTL function 범위의 명시적인 ID를 받는 `NTL_ADD_CALLBACK_ID_N`을 사용하세요.

취소 확인 등으로 callback에서 `ntl::rpc::call_context`가 필요하면
`NTL_ADD_CALLBACK_CONTEXT_N` 계열을 사용합니다. 신뢰하지 않는 요청 인자를
역직렬화하기 전에 호출자 권한을 검사해야 하면
`NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_N` 계열을 사용합니다. 권한 정책은 원본
요청자의 `call_context`를 받고 `NTSTATUS` 또는 `ntl::status`를 반환합니다.
실패 상태이면 인자 역직렬화와 callback 실행이 모두 생략됩니다. 앱 쪽에는 기존과
동일한 동기, 비동기, `stop_token`, coroutine wrapper가 생성됩니다.

가변 크기 return type은
`NTL_RPC_BOUNDED_RESPONSE(Bytes, ReturnType)`로 최대 직렬화 응답 크기를
선언할 수 있습니다. server는 이 크기의 output buffer가 제공됐는지 callback
실행 전에 확인합니다.

타입:

- `ntl::rpc::server`
  - kernel-mode RPC device와 typed dispatch table을 소유합니다.
- `ntl::rpc::client`
  - user-mode client입니다.
  - generated `<name>_<arity>_method`를 받는 `invoke(method, args...)`를
    제공합니다. return type과 response capacity는 method에서 추론합니다.
- `ntl::rpc::method<Id, Signature>`
  - macro 내부에서 사용하는 v142 호환 typed contract입니다.

method, notification, stream의 wire schema fingerprint는 인자와 payload
타입에서 자동으로 계산됩니다. 사용자 정의 타입은 기존
`static serialize(Archive&, Self&)` 필드 목록을 그대로 사용하므로 C++14에서도
별도의 수동 hash 값을 지정하지 않습니다. `contract_version`은 직렬화 타입이
같아도 동작 의미가 달라지는 경우에 올리는 application protocol revision입니다.

신뢰성 notification을 드라이버 unload 또는 재부팅 뒤에도 복구해야 하면
`ntl::rpc::registry_notification_store`를 명시적으로 설치할 수 있습니다. 설치하지
않으면 RPC transport는 registry I/O를 수행하지 않습니다. 저장 key, ACL,
volatile/nonvolatile 정책과 정리는 사용하는 드라이버가 결정합니다.

전체 schema 예시는 [`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp)를
참고하세요. 더 작은 app/driver 흐름 예시는 [NTL 사용 예제](./ko-kr-usage-examples.md)를
참고하세요.

IRQL: server-side callback은 `PASSIVE_LEVEL`로 취급하세요. user-mode
`ntl::rpc::client` 쪽은 커널 IRQL 규칙 밖에 있지만, driver-side dispatch
contract에는 여전히 의존합니다.

## IRQL

헤더: [`include/ntl/irql`](../include/ntl/irql)

`ntl::irql`은 주요 `KIRQL` 값을 감싸는 enum입니다.

Helper:

- `ntl::raised_irql`
  - destructor에서 IRQL을 낮추는 RAII 객체입니다.
- `ntl::raise_irql(KIRQL)`
- `ntl::raise_irql_to_dpc_level()`
- `ntl::raise_irql_to_synch_level()`
- `ntl::current_irql()`

IRQL: 이 helper들은 IRQL을 직접 조작하거나 관찰합니다. RAII 객체의 scope는
최대한 짧게 유지하고, IRQL이 올라간 동안에는 해당 문맥을 문서화한 helper만
호출하세요.

## Kernel Coroutine Context

헤더: [`include/ntl/coroutine`](../include/ntl/coroutine)

C++20의 `co_await ntl::resume_on_passive()`는 현재 coroutine의 명시적인
continuation을 `PASSIVE_LEVEL`에서 이어가게 합니다. 이미 passive이면 inline
으로 계속하고, `DISPATCH_LEVEL`까지는 `ntl::passive_executor`를 통해 system
worker에 queue합니다.

```cpp
const ntl::status resumed = co_await ntl::resume_on_passive();
if (!resumed.is_ok())
  co_return;

use_passive_only_runtime();
```

queue에 실패하면 coroutine은 원래 caller context에서 계속되어 실패 status를
받습니다. 따라서 passive-only 작업 전에 반환값을 반드시 검사해야 합니다.
이 helper는 임의의 `std::coroutine_handle::resume()`를 가로채지 않으며, raw
resume은 호출한 thread의 execution context에서 실행됩니다. coroutine frame과
captured state는 queued continuation이 끝날 때까지 살아 있어야 하고, unload
전에 outstanding continuation을 모두 drain해야 합니다.

상세 계약과 driver test 범위는
[Kernel coroutine context](./ntl/coroutine.md)를 참고하세요.

## Spin Lock

헤더: [`include/ntl/spin_lock`](../include/ntl/spin_lock)

`ntl::spin_lock`은 `KSPIN_LOCK`을 감쌉니다.

- `try_lock()`
- `lock()`
- `unlock()`
- `lock_at_dpc_level()`
- `unlock_from_dpc_level()`
- `test()`
- `native_handle()`

`ntl::unique_lock<ntl::spin_lock>`은 `std::unique_lock`을 확장하며
`ntl::at_dpc_level_lock`을 지원합니다.

IRQL: `lock()`과 성공한 `try_lock()`은 `DISPATCH_LEVEL`로 올리고,
`unlock()`은 이전 IRQL을 복원합니다. `lock_at_dpc_level()`과
`unlock_from_dpc_level()`은 caller가 이미 `DISPATCH_LEVEL`에서 실행 중이어야
합니다. spin lock을 잡은 동안 실행되는 코드는 resident, short, nonblocking이어야
하며 allocation, wait, throw, arbitrary runtime/STL helper 호출을 하면 안 됩니다.

## ERESOURCE

헤더: [`include/ntl/resource`](../include/ntl/resource)

`ntl::resource`는 `ERESOURCE`를 감쌉니다.

- `try_lock()`
- `try_lock_shared()`
- `lock()`
- `unlock()`
- `lock_shared()`
- `unlock_shared()`
- `locked() const`
- `locked_exclusive() const`
- `locked_shared() const`
- `lock_no_critical_region(bool wait = true)`
- `lock_shared_no_critical_region(bool wait = true)`
- `unlock_no_critical_region()`
- `unlock_shared_no_critical_region()`
- `convert_to_shared()`
- `waiter_count() const`
- `shared_waiter_count() const`
- `native_handle()`

Lock helper:

- `ntl::unique_lock<ntl::resource>`
- `ntl::shared_lock<ntl::resource>`
- `ntl::adopt_critical_region`

IRQL: `<= APC_LEVEL`. 이 helper는 blocking/resource-style synchronization
모델에 맞춥니다. DPC, ISR, spin-lock-held path에서는 사용하지 마세요.
`adopt_critical_region`은 caller가 critical region boundary를 의도적으로
직접 관리할 때 사용합니다.

## Unicode String

헤더: [`include/ntl/unicode_string`](../include/ntl/unicode_string)

`ntl::unicode_string`은 `std::wstring`을 `UNICODE_STRING`으로 사용할 수 있게
도와줍니다.

- `std::wstring`으로 생성
- `c_str() const`
- `operator*()`

IRQL: `std::wstring`으로부터 생성하는 경로는 `PASSIVE_LEVEL`로 취급하세요.
accessor는 가볍지만 lifetime과 storage는 owning C++ object에 속합니다.
