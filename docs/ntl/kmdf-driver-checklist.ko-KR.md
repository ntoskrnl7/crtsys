# KMDF 드라이버 엔지니어링 체크리스트

[KMDF API 가이드로 돌아가기](./kmdf.ko-KR.md)

이 체크리스트는 실제 드라이버를 검토할 수 있는 규칙으로 `ntl::kmdf` 객체 모델을
정리합니다. 네이티브 WDF 콜백, IRQL, 동기화, PnP, 전원, 취소 계약을 대체하는 것이
아니라 보완합니다.

## 소유권 표

| 값 | 소유자 | 유효성 및 이전 규칙 |
| --- | --- | --- |
| `driver`, `device`, `file`, `io_queue`, `io_target`, `memory`, `timer`, `work_item`, `interrupt`, `dma_*`, `wmi_*` | WDF | 비소유 facade입니다. 네이티브 WDF 객체의 수명 너머로 보관하지 마세요. 오래 사는 자식에는 부모를 명시적으로 지정하세요. |
| 큐가 전달한 `request` | 하나의 종료 동작 전까지 드라이버 콜백 | 완료, 전달, 재큐잉, 전송할 수 있는 이동 전용 권한입니다. 성공적으로 이전하면 원본 facade는 무효가 됩니다. |
| WDF 수동 큐 안의 요청 | WDF | 드라이버 소유로 취급하기 전에 꺼내야 합니다. 큐에 있는 동안 취소된 요청은 큐 취소 콜백이 완료하게 하세요. |
| `found_request` | 요청 소유권이 아닌 드라이버 소유 객체 참조 | 소멸 시 역참조합니다. `try_retrieve()`가 드라이버 소유 `request`로의 원자적 전환을 시도합니다. |
| `driver_request` | 드라이버 | 보내지 않은 드라이버 생성 요청을 삭제합니다. 비동기 전송이 성공하면 소유권은 완료 콜백으로 이전됩니다. |
| `registry_key` | 드라이버 | 이동 전용이며 소멸 시 WDF 키를 닫습니다. |
| `queried_interface<T>` | 드라이버 | 이동 전용이며 `InterfaceDereference`를 정확히 한 번 호출합니다. |
| `EvtDriverDeviceAdd`에 전달한 `device_init` | WDF | 비소유입니다. 장치 생성이 성공하면 소비하고, 콜백이 먼저 반환하면 KMDF가 정리합니다. |
| `control_device_init` 또는 할당한 `pdo_init` | 장치 생성 전까지 드라이버 | 이동 전용 소유자입니다. 소멸 시 소비하지 않은 초기화 상태를 해제합니다. |
| C++ 객체 컨텍스트 | WDF 객체 | 컨텍스트 할당 뒤에 생성되고 WDF destroy 콜백에서 소멸합니다. 컨텍스트 생성자와 소멸자는 `noexcept`여야 합니다. |

빌린 요청 버퍼 포인터, 리소스 목록 뷰, WMI 버퍼, 콜백 인수는 문서화된 WDF 수명이
끝난 뒤 보관하지 마세요.

## 요청 상태 기계

```text
queue callback
    |
    +-- complete ------------------------------> done
    |
    +-- forward/requeue/send succeeds --------> WDF owns it
    |
    +-- retain outside WDF queue
            |
            +-- mark cancelable
                    |
                    +-- cancel callback -------> completes exactly once
                    |
                    +-- unmark succeeds -------> retaining path completes
                    |
                    +-- unmark = STATUS_CANCELLED
                                                cancel callback completes
```

- 요청이 WDF 큐에 남아 있을 때는 cancelable로 표시하지 마세요.
- 꺼낸 뒤 보관한다면 `try_mark_cancelable()` 전에 영속 상태를 마련하세요. 취소가
  즉시 경쟁할 수 있습니다.
- 취소 콜백에서 보관 상태를 지우기 전에 완료를 발생시키는 타이머, 인터럽트 또는
  target 콜백을 중지하세요.
- `try_unmark_cancelable()`가 반환한 `STATUS_CANCELLED`는 취소 콜백이 완료를
  소유한다는 뜻입니다. 그 요청을 다시 건드리거나 완료하지 마세요.
- 이동 한정 forward, requeue, send가 실패하면 드라이버가 원래 요청을 계속 소유하므로
  다른 종료 동작을 선택해야 합니다.

## 버퍼와 ABI 규칙

- 사용자/커널 계약에는 고정 폭 필드를 쓰고 모든 교차 비트 수 레이아웃을
  `static_assert`로 검증하세요.
- 제품 ABI에는 크기와 버전을 넣고, 뒤의 필드를 읽기 전에 지원하지 않는 버전을
  거부하세요.
- WDF가 보고한 버퍼 크기와 내부 계약 크기를 모두 검증하세요.
- `METHOD_BUFFERED`에서는 입력과 출력이 같은 시스템 버퍼를 alias할 수 있습니다.
  출력 구조를 0으로 만들거나 쓰기 전에 모든 입력 필드를 복사해 두세요.
- `try_unsafe_user_*()`는 `EvtIoInCallerContext`에서만 사용하고, 사용자 메모리를
  보관하기 전에 잠그거나 복사하세요.
- 정확히 초기화한 출력 바이트 수로 완료하세요.
- IOCTL 접근 비트는 최소 권한으로 유지하고 INF/장치 ACL 정책을 위협 모델과
  일치시키세요.

빌드 가능한 [참조 드라이버](../../examples/kmdf/reference)는 버전이 있는 ABI 및 x64와
WOW64 클라이언트로 이 규칙을 보여 줍니다.

## 콜백과 실행 규칙

- 콜백 템플릿은 정확한 시그니처의 캡처 없는 `noexcept` 함수를 요구합니다. 지속 상태는
  람다 closure가 아니라 WDF context에 둡니다.
- 검토한 CRT/STL 기능을 쓰기 전에 `WdfExecutionLevelPassive`를 선택하세요.
- passive 타이머는 one-shot이어야 합니다. passive 부모와 자동 직렬화를 쓴다면 타이머
  객체 실행 수준도 passive로 설정하세요.
- 동기 queue drain/purge, work-item flush, `timer.stop(true)`는 PASSIVE 수준
  작업이므로 현재 실행 중인 콜백을 기다리면 안 됩니다.
- ISR 및 interrupt 동기화 콜백은 interrupt DIRQL 계약을 따릅니다. 복잡한 작업은 DPC
  또는 passive work item으로 옮기세요.
- 공유 상태에는 WDF 객체/큐 직렬화, interrupt lock, WDF spin/wait lock, 문서화한
  프로토콜을 갖춘 atomic 중 하나만 동기화 소유자로 선택하세요. 우발적인 중첩은
  피하세요.

## PnP 및 전원 수명 주기

```text
DeviceAdd
  -> PrepareHardware
  -> D0Entry
  -> I/O
  -> D0Exit
  -> ReleaseHardware
```

이 순서는 반복될 수 있습니다. surprise removal, 시작 실패, rebalance, restart, sleep,
부분 초기화 때문에 모든 정방향 전이에 성공한 선행 전이가 대응되는 것은 아닙니다.

- `PrepareHardware`에서 변환된 하드웨어 리소스를 획득하고, `ReleaseHardware`에서는
  실제로 획득한 것만 해제하세요.
- 부분 실패에 대해 D0 entry/exit가 멱등적이게 하세요.
- 타이머, 인터럽트, DMA 또는 하위 target 완료가 쓰는 상태를 정리하기 전에 새 I/O를
  중지하세요.
- 소프트웨어 열거 장치에 없는 리소스를 만들어 내지 마세요.
- 필터는 자신이 소유하지 않는 요청을 전달하고, 계약상 의도한 변환이 아니라면 하위
  스택의 status 및 information을 보존합니다.
- 버스는 자식의 identity와 presence를 소유하고, 자식 function driver는 기능 정책을
  소유합니다. 다른 ABI처럼 드라이버 정의 query interface에도 버전을 부여하세요.

## 출시 게이트

소프트웨어 전용 변경은 다음을 모두 통과해야 준비된 것입니다.

1. x86 및 x64 컴파일 계약이 `/W4 /WX`로 빌드된다.
2. x64 드라이버 패키지가 INF/catalog/signability 검사를 통과한다.
3. x64와 WOW64 애플리케이션이 반환 상태를 검증한다.
4. 설치, 장치 restart, 제거, 취소, 반복 실행이 통과한다.
5. 선택한 바이너리가 `verifier /query`에서 활성 상태로 보인다.
6. Driver Verifier가 verifier breakpoint 또는 bugcheck 없이 load 및 Special Pool
   할당 같은 실제 활동을 기록한다.
7. Verifier 부팅 뒤 새 dump 또는 예기치 않은 reboot 이벤트가 없다.
8. 테스트 장치가 없고 이전 Verifier 구성이 복원된다.

DMA, USB, interrupt, PCI, firmware, wake, class-extension 드라이버에는 별도의
하드웨어 게이트가 필요합니다. 컴파일 전용 템플릿은 런타임 근거가 아닙니다.

저장소의 [소프트웨어 전용 런타임 픽스처](../../test/kmdf/runtime)는 이 게이트의 반복
가능한 VM 부분을 구현합니다. 게스트는 일회용으로 유지하고 Verifier 또는 stress
작업에는 커널 디버거를 연결하세요.
