# NTL 커널 코루틴 실행 문맥

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/coroutine`](../../include/ntl/coroutine)

`ntl::resume_on_passive()`는 `PASSIVE_LEVEL`에서 계속 실행해야 하는 커널 코루틴을 위한 선택적 C++20 awaiter입니다. [`ntl::passive_executor`](./passive-executor.ko-KR.md)를 사용하며 다음과 같이 동작합니다.

- `PASSIVE_LEVEL`에서는 코루틴이 인라인으로 계속 실행됩니다.
- `DISPATCH_LEVEL`까지는 코루틴을 일시 중단하고 `PASSIVE_LEVEL`의 시스템 작업자 스레드에서 재개할 수 있습니다.
- continuation을 큐에 넣을 수 없으면 `co_await`가 원래 호출자의 실행 문맥에서 실패한 `ntl::status`를 반환합니다.

```cpp
#include <ntl/coroutine>

kernel_task process_later() {
  const ntl::status resumed = co_await ntl::resume_on_passive();
  if (!resumed.is_ok())
    co_return;

  // This path runs at PASSIVE_LEVEL.
  use_passive_only_runtime();
}
```

passive 수준에서만 가능한 작업을 수행하기 전에 항상 반환 상태를 확인하십시오. 큐 등록 실패 시 코루틴을 다른 실행 문맥으로 옮길 수 없으므로, `co_await` 직후의 코드는 먼저 원래 실행 문맥에서 실패를 확인합니다.

기존 executor 정책을 명시적으로 전달할 수도 있습니다.

```cpp
ntl::passive_executor executor{DelayedWorkQueue, "CORw"};
const ntl::status resumed = co_await ntl::resume_on_passive(executor);
if (!resumed.is_ok())
  co_return;
```

## 적용 범위

이 도우미는 `resume_on_passive()`를 명시적으로 await하는 continuation만 제어합니다. C++ 코루틴 모델을 변경하지 않으며 임의의 `std::coroutine_handle::resume()`을 가로채지도 않습니다. 원시 resume은 이를 호출한 스레드의 실행 문맥에서 동작합니다.

NTL은 코루틴 반환 형식을 강제하거나 코루틴 프레임을 소유하지 않습니다. 작업 소유자는 큐에 등록된 continuation이 끝날 때까지 프레임, executor, 캡처된 상태와 드라이버 이미지를 유지해야 합니다. 드라이버 언로드 시에는 새로운 작업을 중지하고, 해당 상태를 해제하거나 코드를 언로드하기 전에 보류 중인 모든 코루틴 continuation을 drain해야 합니다.

## 진단과 테스트 범위

Debug 빌드에서는 게시된 continuation이 `PASSIVE_LEVEL`보다 높은 수준에서 실행될 경우 경고를 보고합니다. 이는 NTL passive-resume 경로를 위한 진단이며 전역 코루틴 후크가 아닙니다.

드라이버 의미 테스트는 `DISPATCH_LEVEL`에서 코루틴을 시작하고 `resume_on_passive()`를 await한 뒤 완료를 기다려 continuation이 `PASSIVE_LEVEL`에서 실행되었는지 확인합니다.
