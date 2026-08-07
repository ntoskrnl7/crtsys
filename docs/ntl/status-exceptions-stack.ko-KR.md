# NTL 상태, 예외 및 스택 확장

[NTL 문서로 돌아가기](./README.ko-KR.md)

이 문서는 드라이버 제어 경로에 유용한 작은 런타임 관련 도우미, 즉
`ntl::status`, `ntl::exception`, `ntl::seh::try_except`, `ntl::expand_stack`를
다룹니다. 값 또는 상태를 함께 반환하는 형식은 [`ntl::result`](./result.ko-KR.md)를
참조하세요.

## `ntl::status`

헤더: [`include/ntl/status`](../../include/ntl/status)

`ntl::status`는 `NTSTATUS`를 감싼 값 전용 래퍼입니다.

API:

- `status(NTSTATUS status)`
- `bool is_ok() const`
- `bool is_info() const`
- `bool is_warn() const`
- `bool is_err() const`
- `operator NTSTATUS() const`
- `static status ok()`

예제:

```cpp
ntl::status create_control_device(ntl::driver& driver) {
  ntl::device_options options;
  options.name(L"\\Device\\demo").type(FILE_DEVICE_UNKNOWN);

  auto device = driver.create_device<void>(options);
  if (!device) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  return ntl::status::ok();
}
```

IRQL: 값 전용 작업은 할당하거나 대기하지 않습니다. 다만 특정 상태 흐름이 현재
IRQL에서 유효한지는 주변 WDK 호출 경로가 결정합니다.

## 결과

헤더: [`include/ntl/result`](../../include/ntl/result)

`ntl::result<T>`는 [NTL Result](./result.ko-KR.md)에 별도로 문서화되어 있습니다.
도우미가 성공 시 값을 반환하고 실패 시 `NTSTATUS`를 보존해야 할 때 사용합니다.

## 예외

헤더: [`include/ntl/except`](../../include/ntl/except)

형식 및 도우미:

- `ntl::exception`
  - `ntl::status`를 저장합니다.
  - 설명 메시지를 담습니다.
- `ntl::seh::try_except(fn, args...)`
  - `__try` / `__except`를 사용하여 MSVC SEH 내부에서 callable을 실행합니다.
  - 성공 시 `{true, 0}`을, 실패 시 `GetExceptionCode()`의
    `{false, exception_code}`를 반환합니다.

예제:

```cpp
auto [ok, code] = ntl::seh::try_except([&] {
  probe_and_copy_user_buffer();
});

if (!ok) {
  return static_cast<NTSTATUS>(code);
}
```

`try_except`는 SEH 경계를 국소적으로 유지하는 용도입니다. 이를 근거로 임의의 C++
예외나 SEH 예외가 WDK 콜백 경계를 넘어 전파되도록 해서는 안 됩니다.

IRQL: C++ 예외 경로는 `PASSIVE_LEVEL` 전용으로 취급하세요. 정확한 동작을 별도로
테스트하고 문서화하지 않았다면 WDK 콜백 경계, 스핀 잠금을 보유한 구역, DPC, ISR,
페이지 I/O 경로를 가로질러 예외를 던지지 마세요.

## 스택 확장

헤더: [`include/ntl/expand_stack`](../../include/ntl/expand_stack)

기본 커널 스택보다 더 많은 스택이 필요할 수 있는 호출 경로에는
`ntl::expand_stack`를 사용합니다.

API:

- `ntl::expand_stack_size_max`
- `ntl::expand_stack_options`
  - `stack_size(size_t)`
  - `wait(bool)`
  - `ignore_failure(bool)`
- `ntl::expand_stack(options, fn, args...)`

예제:

```cpp
auto options = ntl::expand_stack_options()
                   .stack_size(ntl::expand_stack_size_max)
                   .wait(true);

auto result = ntl::expand_stack(std::move(options), [] {
  return parse_large_control_payload();
});
```

`expand_stack`는 예외 처리나 STL 사용이 많은 제어 경로에 유용합니다. 그렇다고
성능 경로를 작고 예측 가능하게 유지해야 할 필요가 없어지는 것은 아닙니다.

IRQL: 문서화된 `crtsys` 사용은 `PASSIVE_LEVEL`입니다. 래퍼는 C++ 런타임 경로를
사용하며 실패 시 예외를 던질 수 있습니다.
