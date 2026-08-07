# NTL 패시브 실행기

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/passive_executor`](../../include/ntl/passive_executor)

`ntl::passive_executor`는 [`ntl::work_item`](./work-item.ko-KR.md) 위의 정책 계층입니다. queue에 넣은 하나의 work item을 직접 소유하려면 `ntl::work_item`을 사용하십시오. 코드 경로에서 “이 callable을 `PASSIVE_LEVEL`에서 실행하되, 이미 그 수준이면 인라인으로 실행하고 아니면 시스템 worker thread로 연기한다”고 표현하려면 `ntl::passive_executor`를 사용합니다.

## 기본 사용법

```cpp
ntl::passive_executor executor;

auto status = executor.execute([&] {
  // Runs inline if the caller is already at PASSIVE_LEVEL.
  // Otherwise it is copied/moved to nonpaged pool and posted as work item.
  do_passive_only_work();
});

if (!status.is_ok()) {
  return status;
}
```

## 호출자가 소유하는 작업

호출자가 명시적인 완료 상태가 필요하거나 작업을 기다리려면, 호출자가 소유하는 `ntl::passive_work_item`을 만들고 executor가 이를 queue에 넣게 하십시오.

```cpp
auto item = executor.make_work_item([&] {
  do_passive_only_work();
});

auto status = executor.queue_and_wait(item);
if (!status.is_ok()) {
  return status;
}
```

## DPC에서 PASSIVE_LEVEL로 handoff

DPC callback은 `DISPATCH_LEVEL`에서 실행됩니다. DPC는 짧게 유지해야 합니다. 상주 상태만 capture하고, 런타임 사용이 많은 작업은 passive executor에 게시하십시오.

```cpp
struct dpc_context {
  ntl::passive_executor* executor = nullptr;
  ntl::event completed;
  std::atomic<long> value = 0;
};

void on_dpc(void* context, void*, void*) noexcept {
  auto* state = static_cast<dpc_context*>(context);

  (void)state->executor->post([state] {
    // This runs later on a system worker thread at PASSIVE_LEVEL.
    state->value.store(42);
    state->completed.set();
  });
}
```

executor와 capture된 상태는 게시한 작업이 완료될 때까지 유효해야 합니다. 드라이버 unload 경로에서는 DPC 원본을 cancel/drain하고, 상태를 해제하거나 코드를 unload하기 전에 게시된 passive 작업이 끝날 때까지 기다려야 합니다.

C++20 coroutine에서는 [`ntl::resume_on_passive()`](./coroutine.ko-KR.md)가 같은 executor 정책으로 하나의 명시적 continuation을 `PASSIVE_LEVEL`로 옮깁니다.

## API

- `ntl::passive_executor(queue_type = DelayedWorkQueue, tag = default_pool_tag)`
- `queue_type()`
- `tag()`
- `execute(callable)`
- `post(callable)`
- `queue(passive_work_item&)`
- `queue_and_wait(passive_work_item&)`
- `make_work_item(callable)`

## IRQL

- `execute(callable)`:
  - `PASSIVE_LEVEL`에서는 인라인으로 실행합니다.
  - 아직 passive 수준이 아닌 경우 `<= DISPATCH_LEVEL`에서 detached 작업을 게시합니다.
  - `DISPATCH_LEVEL`보다 높으면 `STATUS_INVALID_DEVICE_STATE`를 반환합니다.
- `post(callable)`은 `<= DISPATCH_LEVEL`에서 호출할 수 있습니다.
- `queue_and_wait(item)`은 `PASSIVE_LEVEL` 전용입니다.
- worker callback은 `PASSIVE_LEVEL`에서 실행됩니다.

분리된 `post()`와 passive 수준이 아닌 `execute()`는 callable 저장소를 nonpaged pool에서 할당하고 worker callback이 반환한 뒤 해제합니다. worker가 실행될 때 callable과 그 callable이 접근하는 모든 것은 여전히 유효해야 합니다. 드라이버 unload 경로는 queue된 작업이 드라이버 이미지가 사라진 뒤 실행되지 않게 계속 drain하거나 다른 방식으로 막아야 합니다.

## 드라이버 테스트 범위

드라이버 테스트는 다음을 검사합니다.

- `PASSIVE_LEVEL`에서 인라인 `execute()`
- worker thread의 `PASSIVE_LEVEL`에서 실행되는 분리 `post()`
- 올라간 IRQL에서 worker thread로 연기되는 `execute()`
- `post()`를 통한 DPC callback handoff
- `queue_and_wait()`를 통한 호출자 소유 `passive_work_item`
- `DISPATCH_LEVEL`에서 `PASSIVE_LEVEL`로 이동하는 C++20 coroutine continuation
