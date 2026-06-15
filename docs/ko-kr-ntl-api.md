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
  - callable을 SEH 안에서 실행합니다.
  - `{success, exception_code}`를 반환합니다.

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

RPC helper는 `DeviceIoControl` 기반의 server/client stub을 생성합니다.
직렬화는 `zpp::serializer`를 사용합니다.

주요 macro:

- `NTL_RPC_BEGIN(name)`
- `NTL_RPC_END(name)`
- `NTL_ADD_CALLBACK_0`
- `NTL_ADD_CALLBACK_1`
- `NTL_ADD_CALLBACK_2`
- `NTL_ADD_CALLBACK_3`
- `NTL_ADD_CALLBACK_4`
- `NTL_ADD_CALLBACK_5`

타입:

- `ntl::rpc::server`
  - kernel-mode RPC device holder입니다.
- `ntl::rpc::client`
  - user-mode client입니다.
  - `invoke<Ret>(index, args...)`를 제공합니다.

전체 예시는 [`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp)를 참고하세요.

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
