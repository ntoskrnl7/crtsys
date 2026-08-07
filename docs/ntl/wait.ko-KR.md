# NTL 대기 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/wait`](../../include/ntl/wait)

`ntl::wait` 도우미는 [`ntl::event`](./event.ko-KR.md), [`ntl::timer`](./timer.ko-KR.md), [`ntl::system_thread`](./system-thread.ko-KR.md)처럼 이미 `wait(LARGE_INTEGER*)`를 제공하는 NTL 래퍼에 공통으로 사용할 수 있는 간단한 표현을 제공합니다.

이 도우미는 기반 WDK 대기 모델을 숨기지 않습니다. 반복되는 timeout 준비 코드를 없애고 timeout과 signal 검사를 명시적으로 만들 뿐입니다.

## 기본 사용법

```cpp
ntl::event completed;

if (ntl::wait_timed_out(ntl::try_wait(completed))) {
  return STATUS_PENDING;
}

auto status = ntl::wait_for(completed, 1000);
if (ntl::wait_signaled(status)) {
  return STATUS_SUCCESS;
}

return status;
```

## API

- `ntl::zero_timeout()`
- `ntl::relative_timeout_ms(milliseconds)`
- `ntl::wait_signaled(status)`
- `ntl::wait_timed_out(status)`
- `ntl::try_wait(waitable)`
- `ntl::wait_for(waitable, milliseconds)`

`waitable`은 `ntl::event`, `ntl::timer`, `ntl::system_thread`를 포함하여 `wait(LARGE_INTEGER*)`를 제공하는 모든 NTL 객체입니다.

## IRQL

이 도우미는 대기 대상 객체 자체의 IRQL 계약을 따릅니다. NTL에서 블로킹 대기는 제어 경로 연산이며, 정확한 WDK 프리미티브와 timeout 방식을 별도로 검토하지 않았다면 `PASSIVE_LEVEL`로 취급해야 합니다.

`try_wait()`는 0 timeout을 사용하므로 블로킹되지 않지만, 기반 WDK 대기 호출과 똑같이 동기화 이벤트/타이머 signal을 소비할 수 있습니다.

## 드라이버 테스트 범위

드라이버 테스트는 다음 항목을 검사합니다.

- `ntl::try_wait`를 통한 0 timeout 검사
- signal 및 timeout 상태 분류
- `ntl::timer`를 사용한 `ntl::wait_for`
- `ntl::system_thread`를 사용한 `ntl::wait_for`
