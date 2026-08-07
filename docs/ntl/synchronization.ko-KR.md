# NTL 동기화

[NTL 문서로 돌아가기](./README.ko-KR.md)

이 문서는 IRQL, spin lock, ERESOURCE를 감싼 NTL 도우미를 다룹니다.

## IRQL 도우미

헤더: [`include/ntl/irql`](../../include/ntl/irql)

`ntl::irql`은 일반적인 `KIRQL` 값을 감싼 enum입니다.

도우미:

- `ntl::raised_irql`
  - 소멸자에서 IRQL을 낮추는 RAII 객체
- `ntl::raise_irql(KIRQL)`
- `ntl::raise_irql_to_dpc_level()`
- `ntl::raise_irql_to_synch_level()`
- `ntl::current_irql()`
- `ntl::is_passive_level()`
- `ntl::is_irql_at_most(maximum)`
- `ntl::require_passive_level()`
- `ntl::require_irql_at_most(maximum)`

예제:

```cpp
auto raised = ntl::raise_irql_to_dpc_level();
// Do a short audited DPC-level operation here.
```

계약 확인 예제:

```cpp
ntl::status query_runtime_backed_state() {
  auto s = ntl::require_passive_level();
  if (!s.is_ok()) {
    return s;
  }

  // Safe place for code documented as PASSIVE_LEVEL-only.
  return ntl::status::ok();
}
```

IRQL: 이 도우미는 IRQL을 명시적으로 조작하거나 관찰합니다. 상승된 범위는 가능한
작게 유지하세요. 현재 IRQL이 요청한 계약을 어기면 `require_passive_level()` 및
`require_irql_at_most()`는 예외를 던지지 않고 `STATUS_INVALID_DEVICE_STATE`를
반환합니다.

## 스핀 락

헤더: [`include/ntl/spin_lock`](../../include/ntl/spin_lock)

`ntl::spin_lock`은 `KSPIN_LOCK`을 감쌉니다.

API:

- `try_lock()`
- `lock()`
- `unlock()`
- `lock_at_dpc_level()`
- `unlock_from_dpc_level()`
- `test()`
- `native_handle()`

`ntl::unique_lock<ntl::spin_lock>`은 `ntl::at_dpc_level_lock`으로
`std::unique_lock`을 확장합니다.

예제:

```cpp
ntl::spin_lock lock;

{
  ntl::unique_lock guard(lock);
  // Resident, short, nonblocking work only.
}
```

DPC 수준 예제:

```cpp
auto raised = ntl::raise_irql_to_dpc_level();
ntl::unique_lock guard(lock, ntl::at_dpc_level_lock);
```

IRQL: `lock()`과 성공한 `try_lock()`은 `DISPATCH_LEVEL`까지 올리고,
`unlock()`은 이전 IRQL을 복원합니다. `lock_at_dpc_level()`과
`unlock_from_dpc_level()`은 호출자가 이미 `DISPATCH_LEVEL`에서 실행 중이어야
합니다.

spin lock을 든 동안 실행하는 코드는 resident이고 짧으며 nonblocking이어야 합니다.
할당, 대기, 예외 발생, 임의의 runtime/STL 도우미 호출을 해서는 안 됩니다.

## ERESOURCE

헤더: [`include/ntl/resource`](../../include/ntl/resource)

`ntl::resource`는 `ERESOURCE`를 감쌉니다.

API:

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

잠금 도우미:

- `ntl::unique_lock<ntl::resource>`
- `ntl::shared_lock<ntl::resource>`
- `ntl::adopt_critical_region`

예제:

```cpp
ntl::resource resource;

{
  ntl::shared_lock read_lock(resource);
  // Read shared state.
}

{
  ntl::unique_lock write_lock(resource);
  // Update shared state.
}
```

IRQL: 차단/리소스형 동기화 모델에 맞춰 `<= APC_LEVEL`입니다. DPC, ISR 또는 spin
lock을 든 경로에서 `ntl::resource`를 쓰지 마세요.

`adopt_critical_region`은 호출자가 critical region 경계를 의도적으로 직접 관리할
때 사용합니다.
