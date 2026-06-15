# NTL API 문서

[한국어 문서로 돌아가기](./ko-kr.md)

NTL은 `crtsys`가 함께 제공하는 작은 C++ helper 레이어입니다. WDK 객체를
숨기기보다는, 드라이버 코드에서 RAII ownership, callback 등록, user-mode와
kernel-mode 사이의 간단한 RPC를 쓰기 쉽게 만드는 것을 목표로 합니다.

헤더는 [`include/ntl`](../include/ntl)에 있습니다.

## 진입점

`CRTSYS_NTL_MAIN`을 켜면 다음 함수를 구현합니다.

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys`는 WDK driver entry point를 이 함수로 연결합니다. wrapper는
`ntl::main` 호출 전에 stack expansion helper도 사용합니다.

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

## Exceptions

헤더: [`include/ntl/except`](../include/ntl/except)

- `ntl::exception`
  - `ntl::status`를 보관합니다.
  - 설명 메시지를 함께 가집니다.
- `ntl::seh::try_except(fn, args...)`
  - callable을 SEH 안에서 실행합니다.
  - `{success, exception_code}`를 반환합니다.

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

전체 예시는 [`test/common/rpc.hpp`](../test/common/rpc.hpp)를 참고하세요.

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

## Unicode String

헤더: [`include/ntl/unicode_string`](../include/ntl/unicode_string)

`ntl::unicode_string`은 `std::wstring`을 `UNICODE_STRING`으로 사용할 수 있게
도와줍니다.

- `std::wstring`으로 생성
- `c_str() const`
- `operator*()`
