# NTL 제거 잠금

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/remove_lock`](../../include/ntl/remove_lock)

`ntl::remove_lock`은 `IO_REMOVE_LOCK`을 감쌉니다. 제거, 언로드 또는 정리 경로가
마지막 참조를 해제하기 전에 반드시 끝나야 하는 디스패치 경로에 사용하세요.

## 예제

```cpp
class device_state {
public:
  ntl::remove_lock remove_lock{"RMVl"};
};

device.on_device_control([](const ntl::device_control::code&,
                            const ntl::device_control::in_buffer&,
                            ntl::device_control::out_buffer&) {
  auto guard = state.remove_lock.acquire(current_irp);
  if (!guard) {
    complete_irp(guard.status());
    return;
  }

  // Work is protected until guard leaves scope.
});

driver.on_unload([&] {
  state.remove_lock.release_and_wait();
});
```

## API 요약

- `ntl::remove_lock(tag, max_locked_minutes, high_watermark)`
- `acquire(tag) -> ntl::result<ntl::remove_lock_guard>`
- `release(tag)`
- `release_and_wait(tag)`
- `native_handle()`
- `ntl::remove_lock_guard::reset()`

일반적인 디스패치 코드에서는 `release()`를 수동 호출하기보다 `acquire()`가 반환한
guard를 사용하세요. `release_and_wait()`는 객체가 새 작업을 받지 않게 된 뒤의
정리 경로용입니다. `release_and_wait()`가 성공하면 래퍼는 종료 상태가 됩니다.
그 뒤 `acquire()`를 호출하면 네이티브 remove lock 상태를 다시 건드리지 않고
`STATUS_DELETE_PENDING`을 반환합니다. 이 래퍼는
`IoReleaseRemoveLockAndWait`를 호출하기 전에 제거 경로의 네이티브 acquire를
내부에서 수행하므로, 호출자가 그 WDK 사전 조건만 충족하려고 별도 guard를
유지할 필요가 없습니다.

## IRQL

WDK `IO_REMOVE_LOCK` 계약을 따르세요. 대기할 수 없는 경로에서는
`release_and_wait()`를 호출하지 마세요.

## 드라이버 테스트 범위

드라이버 테스트 모음은 다음을 검증합니다.

- acquire 성공
- guard 이동과 reset
- 여러 acquire 참조
- `release_and_wait`
- 제거 뒤 acquire 거부
