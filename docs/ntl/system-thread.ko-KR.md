# NTL 시스템 스레드 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/system_thread`](../../include/ntl/system_thread)

`ntl::system_thread`는 `PsCreateSystemThread`가 반환한 handle을 소유하고 `ZwClose`로 닫습니다. 스레드를 강제로 중지하지는 않습니다.

`ntl::wait_for(thread, milliseconds)` 같은 공통 timeout 도우미는 [`ntl::wait`](./wait.ko-KR.md)를 참고하십시오.

드라이버 코드에 네이티브 커널 시스템 스레드가 필요하면서 반환된 스레드 handle의 명확한 C++ 소유권도 원할 때 사용하십시오.

## 기본 사용법

```cpp
struct worker_context {
  std::atomic<long> value = 0;
};

void worker(void* context) {
  auto* state = static_cast<worker_context*>(context);
  state->value.store(42);
  PsTerminateSystemThread(STATUS_SUCCESS);
}

worker_context state;
auto thread = ntl::system_thread::create(worker, &state);
if (!thread) {
  return thread.status();
}

auto status = thread->join();
if (!status.is_ok()) {
  return status;
}
```

`join()`은 스레드 종료를 기다리고 대기에 성공하면 소유한 handle을 닫습니다. 대기가 timeout되면 handle은 계속 소유합니다.

## API

- `ntl::system_thread`
  - `system_thread()`
  - `system_thread(HANDLE)`
  - move 생성 / move 대입
  - `create(routine, context, desired_access, object_attributes, process_handle, client_id)`
  - `get()`
  - `wait(timeout)`
  - `join(timeout)`
  - `close()`
  - `reset(handle = nullptr)`
  - `release()`
  - `valid()` / `operator bool()`

## 수명

`ntl::system_thread`를 소멸하면 handle만 닫습니다. 스레드를 기다리거나 종료하지는 않습니다. 스레드 routine이 스택 상태, 객체, lock, event, STL 컨테이너 또는 드라이버 소유 리소스를 사용한다면 해당 리소스가 소멸하기 전에 스레드를 join하십시오.

## IRQL

`PsCreateSystemThread`, `ZwWaitForSingleObject`, `ZwClose`는 제어 경로 연산입니다. 이 도우미는 `PASSIVE_LEVEL`에서 사용하십시오.

시스템 스레드 routine은 시스템 스레드 실행 문맥의 `PASSIVE_LEVEL`에서 실행되지만, 일반적인 unload/수명 규칙은 그대로 적용됩니다. 해당 스레드는 자신을 소유한 드라이버 이미지가 언로드된 뒤 코드를 실행하거나 데이터에 접근해서는 안 됩니다.

## 드라이버 테스트 범위

드라이버 테스트는 다음을 검사합니다.

- `ntl::system_thread::create`를 통한 `PsCreateSystemThread` 생성
- `PASSIVE_LEVEL`에서의 routine 실행
- `join()` 대기와 handle 닫기
- move 소유권
- `release()` / adopt / `close()` 소유권 이전
