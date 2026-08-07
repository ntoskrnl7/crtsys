# NTL work item 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::work_item`은 WDK `WORK_QUEUE_ITEM`과 `ExQueueWorkItem`을 위한 작은 C++ 래퍼입니다. 완료 상태 추적에는 내부적으로 `ntl::event`를 사용합니다.

드라이버 코드에서 작은 작업 단위를 `PASSIVE_LEVEL`로 실행되는 시스템 worker thread로 연기해야 할 때 사용하십시오.

work item을 직접 소유하는 대신 인라인 실행 또는 연기 정책이 필요하다면 [`ntl::passive_executor`](./passive-executor.ko-KR.md)를 사용하십시오.

헤더: [`include/ntl/work_item`](../../include/ntl/work_item)

## 원시 context 형식

```cpp
#include <ntl/work_item>

struct request_context {
  LONG completed = 0;
};

void do_passive_work(void* context) noexcept {
  auto* request = static_cast<request_context*>(context);

  // This callback runs on a system worker thread at PASSIVE_LEVEL.
  InterlockedExchange(&request->completed, 1);
}

request_context context;
ntl::work_item item(do_passive_work, &context);

auto status = item.queue();
if (status.is_ok()) {
  (void)item.wait();
}
```

## callable 형식

`ntl::passive_work_item<Callable>`은 work-item 객체 안에 callable을 직접 저장합니다. callable의 저장소와 capture가 유효한 동안 생성하고, 연기가 필요할 때 queue에 넣으십시오.

```cpp
volatile LONG completed = 0;

ntl::passive_work_item item([&] {
  InterlockedExchange(&completed, 1);
});

(void)item.queue();
(void)item.wait();
```

## API

`ntl::work_item`:

- `work_item(routine, context = nullptr, queue_type = DelayedWorkQueue)`
- `queue()`
- `wait()`
- `queued()`
- `last_status()`

`ntl::passive_work_item<Callable>`:

- `passive_work_item(callable, queue_type = DelayedWorkQueue)`
- `queue()`
- `wait()`
- `queued()`
- `last_status()`

## IRQL

- `queue()`는 `<= DISPATCH_LEVEL`에서 호출할 수 있습니다.
- `wait()`는 `PASSIVE_LEVEL`에서만 호출할 수 있습니다.
- worker callback은 `PASSIVE_LEVEL`에서 실행됩니다.
- queue에 넣은 item은 `wait()`가 끝난 뒤에만 소멸시키거나, 소멸자가 완료를 기다릴 수 있는 `PASSIVE_LEVEL`에서 소멸시키십시오.
- C++ 예외가 worker callback 밖으로 빠져나가지 않게 하십시오. callback 내부에서 잡아 드라이버가 소유한 status/state로 변환해야 합니다.

이 도우미는 작업을 queue에 넣을 뿐입니다. 높은 IRQL에서 임의의 capture 상태를 안전하게 만들지는 않습니다. callable이 STL 객체, 문자열, smart pointer 또는 런타임 기반 상태를 capture한다면 검토된 실행 문맥에서 그 상태를 생성하고 소유하며, work item이 완료될 때까지 유지하십시오.
