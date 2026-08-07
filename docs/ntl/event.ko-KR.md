# NTL 이벤트 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::event`는 WDK `KEVENT`의 작은 래퍼입니다.

NTL 코드에서 알림 또는 동기화 이벤트가 필요하지만 호출 위치마다 `KeInitializeEvent` / `KeSetEvent` / `KeWaitForSingleObject` 절차를 반복하고 싶지 않을 때 사용하십시오.

헤더: [`include/ntl/event`](../../include/ntl/event)

`ntl::try_wait()`와 `ntl::wait_for()` 같은 공통 timeout 도우미는 [`ntl::wait`](./wait.ko-KR.md)를 참고하십시오.

## 예제

```cpp
#include <ntl/event>

ntl::event completed;

completed.set();

if (completed.signaled()) {
  (void)completed.wait();
}
```

동기화 이벤트:

```cpp
ntl::event one_shot(ntl::event_type::synchronization);
one_shot.set();
(void)one_shot.wait(); // consumes the signal
```

## API

- `event(event_type type = event_type::notification, bool initial_state = false)`
- `native()`
- `set(increment = IO_NO_INCREMENT, wait = false)`
- `reset()`
- `clear()`
- `read_state()`
- `signaled()`
- `wait(timeout = nullptr)`
- `wait(wait_reason, wait_mode, alertable, timeout = nullptr)`

## IRQL

이 도우미는 기반 WDK 이벤트 API의 계약을 따릅니다.

- `set`, `reset`, `clear`, `read_state`는 네이티브 `KEVENT` 계약을 따릅니다.
- 블로킹 `wait()`는 NTL 문서에서 `PASSIVE_LEVEL` 제어 경로 연산입니다.
- timeout이 0인 비블로킹 대기는 더 제한적인 WDK 실행 문맥에서도 유효할 수 있지만, 해당 경로는 별도로 검토해야 합니다.
