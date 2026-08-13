# NTL 패시브 실행기

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/passive_executor`](../../include/ntl/passive_executor)

`ntl::passive_executor`는 [`ntl::work_item`](./work-item.ko-KR.md) 위에 놓인
정책 계층입니다. 큐에 넣은 작업 항목 하나를 직접 소유하려면 `ntl::work_item`을,
호출 가능 객체를 `PASSIVE_LEVEL`에서 실행하되 이미 그 IRQL이면 즉시 실행하고
그렇지 않으면 시스템 워커 스레드로 미루려면 `ntl::passive_executor`를 사용합니다.

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

## 호출자 소유 작업

호출자가 명시적인 완료 상태가 필요하거나 작업을 기다리려면 호출자 소유
`ntl::passive_work_item`을 만들고 실행기가 이를 큐에 넣게 합니다.

```cpp
auto item = executor.make_work_item([&] {
  do_passive_only_work();
});

auto status = executor.queue_and_wait(item);
if (!status.is_ok()) {
  return status;
}
```

## DPC에서 PASSIVE_LEVEL로 넘기기

DPC 콜백은 `DISPATCH_LEVEL`에서 실행됩니다. DPC는 상주 상태만 캡처할 정도로
짧게 유지하고, 런타임 사용량이 큰 작업은 패시브 실행기에 게시하세요.

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

실행기와 캡처한 상태는 게시한 작업이 끝날 때까지 유효해야 합니다. 드라이버 언로드
경로는 DPC 발생원을 취소·drain하고, 상태를 해제하거나 코드를 언로드하기 전에
게시한 패시브 작업이 끝날 때까지 기다려야 합니다.

C++20 코루틴에서는 [`ntl::resume_on_passive()`](./coroutine.ko-KR.md)가 같은
실행기 정책으로 명시적인 continuation 하나를 `PASSIVE_LEVEL`로 옮깁니다.

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
  - `PASSIVE_LEVEL`에서는 즉시 실행합니다.
  - 아직 패시브 수준이 아니면 `<= DISPATCH_LEVEL`에서 분리된 작업을 게시합니다.
  - `DISPATCH_LEVEL`보다 높으면 `STATUS_INVALID_DEVICE_STATE`를 반환합니다.
- `post(callable)`은 `<= DISPATCH_LEVEL`에서 호출할 수 있습니다.
- `queue_and_wait(item)`은 `PASSIVE_LEVEL` 전용입니다.
- 워커 콜백은 `PASSIVE_LEVEL`에서 실행됩니다.

분리된 `post()`와 패시브 수준이 아닌 `execute()`는 호출 가능 객체의 저장소를
nonpaged pool에서 할당하고 워커 콜백이 반환한 뒤 해제합니다. 워커가 실행할 때까지
호출 가능 객체와 그것이 참조하는 모든 대상은 유효해야 합니다. 드라이버 언로드
경로는 큐에 든 작업이 드라이버 이미지가 사라진 뒤 실행되지 않도록 계속 drain하거나
다른 방법으로 막아야 합니다.

## 드라이버 테스트 범위

드라이버 테스트는 다음을 다룹니다.

- `PASSIVE_LEVEL`에서 즉시 실행하는 `execute()`
- 워커 스레드의 `PASSIVE_LEVEL`에서 실행되는 분리된 `post()`
- 올린 IRQL에서 워커 스레드로 연기되는 `execute()`
- `post()`를 통한 DPC 콜백 전달
- `queue_and_wait()`를 통한 호출자 소유 `passive_work_item`
- `DISPATCH_LEVEL`에서 `PASSIVE_LEVEL`로 가는 C++20 코루틴 continuation
