# NTL 결과

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/result`](../../include/ntl/result)

`ntl::result<T>`는 값 또는 `NTSTATUS`를 담습니다. 모든 호출자에게 예외 또는 출력 매개변수를 강제하지 않으면서 커널 스타일 상태 전파를 보존해야 하는 드라이버 도우미에 유용합니다.

## API

- `ntl::result<T>`
  - `result()`
  - `result(T)`
  - `result(ntl::unexpected_status)`
  - `static success(args...)`
  - `static failure(NTSTATUS)`
  - `bool has_value() const`
  - `explicit operator bool() const`
  - `ntl::status status() const`
  - `value()`
  - `operator*`, `operator->`
  - `value_or(fallback)`
- `ntl::result<void>`
  - `static success(NTSTATUS = STATUS_SUCCESS)`
  - `static failure(NTSTATUS)`
  - `bool has_value() const`
  - `status()`
  - `value()`
- `ntl::unexpected(status)`
- `ntl::ok(value)`
- `ntl::ok()`

## 예제

```cpp
ntl::result<ntl::pool_buffer> make_control_buffer(size_t bytes) {
  auto buffer = ntl::make_pool_buffer(bytes, ntl::pool_kind::nonpaged, "NTLb");
  if (!buffer) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  }

  return buffer;
}

auto buffer = make_control_buffer(4096);
if (!buffer) {
  return buffer.status();
}

use_buffer(buffer->get());
```

호출자가 의도적으로 예외 스타일 제어 흐름을 원한다면 `value()`를 사용하십시오.

```cpp
try {
  auto buffer = make_control_buffer(4096).value();
  use_buffer(buffer.get());
} catch (const ntl::exception& e) {
  return e.get_status();
}
```

## IRQL

`has_value()`, `operator bool`, `status()` 확인은 값만 다루므로 호출자 실행 문맥을 따릅니다. 포함된 `T`의 생성, 이동, 소멸, 접근은 `T` 자체의 규칙을 따릅니다. 실패한 result에서 `value()`를 호출하면 `ntl::exception`을 던지므로, 실패한 값 경로는 `PASSIVE_LEVEL`로 취급하십시오.

## 결과를 반환하는 도우미

기존 NTL API는 소스 호환성을 위해 원래 동작을 유지합니다. 도우미가 값을 만들고 실패를 자연스럽게 `NTSTATUS`로 표현할 수 있는 경우 result 반환 variant를 추가합니다.

- `ntl::try_make_pool_buffer(...) -> ntl::result<ntl::pool_buffer>`
- `ntl::try_allocate_pool(...) -> ntl::result<void*>`
- `ntl::try_make_pool<T>(...) -> ntl::result<ntl::pool_ptr<T>>`
- `ntl::driver::try_create_device<T>(...) -> ntl::result<std::shared_ptr<ntl::device<T>>>`
- `ntl::try_create_device_endpoint<T>(...) -> ntl::result<ntl::device_endpoint<T>>`
- `ntl::try_create_symbolic_link(...) -> ntl::result<ntl::symbolic_link>`
- `ntl::registry_key::open(...) -> ntl::result<ntl::registry_key>`
- `ntl::registry_key::create(...) -> ntl::result<ntl::registry_key>`
- `ntl::try_open_driver_parameters(...) -> ntl::result<ntl::registry_key>`

따라서 단순 상태 전용 연산은 `ntl::status`로, 예외 중심 생성자는 생성자로, 값을 만드는 factory 경로는 `ntl::result<T>`로 유지됩니다.
