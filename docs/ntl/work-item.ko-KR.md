# NTL work item 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::work_item`은 WDK `WORK_QUEUE_ITEM`과 `ExQueueWorkItem`을 감싼 작은 C++
래퍼입니다. 완료 상태 추적에는 내부적으로 `ntl::event`를 사용합니다.

드라이버 코드에서 작은 작업 단위를 `PASSIVE_LEVEL`에서 실행되는 시스템 워커
스레드로 미뤄야 할 때 사용하세요.

직접적인 work-item 소유권 대신 즉시 실행 또는 연기 정책이 필요하면
[`ntl::passive_executor`](./passive-executor.ko-KR.md)를 사용하세요.

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

## 호출 가능 객체 형식

`ntl::passive_work_item<Callable>`은 호출 가능 객체를 work-item 객체 안에 직접
저장합니다. 호출 가능 객체의 저장소와 캡처가 유효한 상태에서 생성하고, 연기가
필요할 때 큐에 넣으세요.

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
- `wait()`는 `PASSIVE_LEVEL` 전용입니다.
- 워커 콜백은 `PASSIVE_LEVEL`에서 실행됩니다.
- 큐에 넣은 항목은 `wait()`가 끝난 뒤에만 소멸시키거나, 소멸자가 완료를 기다릴 수
  있는 `PASSIVE_LEVEL`에서 소멸시키세요.
- C++ 예외가 워커 콜백 밖으로 나가지 않게 하세요. 필요하면 콜백 안에서 잡아
  드라이버 소유 상태 또는 상태 코드로 변환합니다.

이 도우미는 작업을 큐에 넣을 뿐입니다. 높은 IRQL에서 임의의 캡처 상태를 안전하게
만들어 주지는 않습니다. 호출 가능 객체가 STL 객체, 문자열, 스마트 포인터, 그 밖의
런타임 기반 상태를 캡처한다면 검토된 문맥에서 그 상태를 생성·소유하고 work item이
끝날 때까지 살려 두세요.
