# NTL 타이머 및 DPC 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/timer`](../../include/ntl/timer)

`ntl::timer`는 `KTIMER`를, `ntl::kdpc`는 `KDPC`를 래핑합니다. 매번 네이티브 초기화 절차를 직접 작성하지 않고 one-shot timer, periodic timer 또는 명시적 DPC queueing이 필요한 드라이버를 위한 얇은 WDK 도우미입니다.

이 도우미는 의도적으로 WDK 모델을 그대로 드러냅니다. DPC callback은 계속 DPC 환경에서 실행되므로 상주 코드로 유지하고, 짧고 비블로킹으로 작성하며, 임의의 STL/CRT 작업을 수행하지 않게 하십시오.

## 일회성 타이머

```cpp
#include <ntl/timer>

ntl::timer timer(ntl::timer_type::synchronization);

timer.set_once(ntl::relative_due_time_ms(100));

auto timeout = ntl::relative_due_time_ms(1000);
auto wait_status = timer.wait(&timeout);
if (!wait_status.is_ok()) {
  return wait_status;
}
```

`ntl::relative_due_time_ms(ms)`는 상대 due time에 대해 `KeSetTimer`와 `KeSetTimerEx`가 사용하는 음수 100ns 단위 `LARGE_INTEGER` 형식을 반환합니다. [`ntl::wait`](./wait.ko-KR.md)의 `ntl::relative_timeout_ms(ms)`는 wait 호출에 동일한 상대 timeout 인코딩을 사용합니다.

## DPC를 사용하는 타이머

```cpp
struct timer_context {
  std::atomic<long> fired = 0;
};

void on_timer(void* context, void*, void*) noexcept {
  auto* state = static_cast<timer_context*>(context);
  state->fired.fetch_add(1);
}

timer_context state;
ntl::kdpc callback(on_timer, &state);
ntl::timer timer;

timer.set_once(ntl::relative_due_time_ms(10), &callback);
```

callback은 `ntl::kdpc` 객체에 저장된 context와 직접 DPC queueing 또는 timer가 제공하는 두 system argument를 받습니다. callback은 반드시 `noexcept`여야 합니다.

## 주기적 타이머

```cpp
ntl::timer timer;
timer.set_periodic(ntl::relative_due_time_ms(1000), 1000, &callback);

// Later, before releasing callback state:
timer.cancel();
```

`timer`는 소멸자에서 자신을 취소합니다. 아직 queue에 남아 있는 timer가 나중에 소멸된 `KTIMER`를 건드리는 일을 막습니다. 그러나 만료된 timer가 이미 queue에 넣은 DPC가 끝났음을 보장하지는 않습니다. DPC가 외부 상태를 소유하거나 접근한다면, 그 상태를 해제하기 전에 드라이버가 해당 callback과 동기화해야 합니다.

## 직접 DPC queueing

```cpp
ntl::kdpc callback(on_timer, &state);
callback.queue();
```

`kdpc::cancel()`은 `KeRemoveQueueDpc`에 대응하며, 실행 전에 queue에 넣은 DPC를 제거했는지 반환합니다. `ntl::kdpc`도 소멸자에서 제거를 시도하지만 이미 실행 중인 callback은 상태를 해제하기 전에 여전히 외부 동기화가 필요합니다.

## API

- `ntl::timer_type`
- `ntl::relative_due_time_ms(milliseconds)`
- [`ntl::wait`](./wait.ko-KR.md)의 `ntl::relative_timeout_ms(milliseconds)`
- `ntl::kdpc`
  - `kdpc(routine, context)`
  - `initialize(routine, context)`
  - `native()`
  - `queue(system_argument1, system_argument2)`
  - `cancel()`
- `ntl::timer`
  - `timer(timer_type)`
  - `native()`
  - `set_once(due_time, kdpc*)`
  - `set_periodic(due_time, period_ms, kdpc*)`
  - `cancel()`
  - `read_state()`
  - `signaled()`
  - `wait(...)`

## IRQL

- timer 설정/취소는 WDK `KeSetTimer*` / `KeCancelTimer` 계약을 따릅니다.
- timer 대기는 NTL에서 블로킹 제어 경로 연산이므로 `PASSIVE_LEVEL`로 취급하십시오.
- DPC callback은 `DISPATCH_LEVEL`에서 실행됩니다. callback에서 paged memory를 할당하거나, 블로킹하거나, 예외를 던지거나, stream을 호출하거나, 임의의 STL/CRT 코드를 실행하지 마십시오.
- DPC 작업에 STL/CRT가 필요하다면 DPC에는 최소 상태만 capture하고 [`ntl::passive_executor`](./passive-executor.ko-KR.md), [`ntl::work_item`](./work-item.ko-KR.md) 또는 다른 `PASSIVE_LEVEL` 경로를 queue에 넣으십시오.

## 드라이버 테스트 범위

드라이버 테스트는 다음을 검사합니다.

- 직접 `ntl::kdpc::queue`
- DPC callback argument 전달
- one-shot synchronization timer 대기
- DPC callback을 사용하는 one-shot timer
- periodic timer 설정 및 취소
