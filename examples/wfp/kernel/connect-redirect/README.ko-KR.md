# 커널 connect-redirect controller

[English](./README.md)

dual-stack WSK proxy는 드라이버가 소유합니다. controller에는 typed WFP
redirect 정책, 드라이버 counter 조회, lifecycle IPC, 통계만 있습니다.

accept된 두 socket은 중첩된 `io::with_async_transport` owner 안에서 실행하고,
두 relay 방향은 `kernel::join_bidirectional`로 결합합니다. provider와 stream의
선언 순서 규칙이나 수동 callback drain 순서가 없습니다. 조기 반환, 취소, 어느
한 방향의 실패도 신규 작업을 닫고 두 transport를 join한 뒤 owning 작업을
완료합니다.

```text
crtsys_wfp_kernel_connect_redirect_controller.exe <origin-port> <ipc-directory>
```

유효한 kernel listener port를 조회하고 두 filter를 설치한 뒤
`controller.ready`를 만듭니다. `stop.request`가 오면 정책을 제거하고 최종 WSK
counter와 opaque redirect record capture 수를 `controller.stats`에 기록합니다.

socket listener, client, traffic, PASS assertion은
`test/wfp/runtime/fixtures/kernel/connect-redirect`에 있습니다. 제품 프로젝트를
빌드하면 controller 옆에 `crtsys_wfp_kernel_connect_redirect_acceptance.exe`도
생성됩니다.
