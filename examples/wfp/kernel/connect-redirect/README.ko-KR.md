# 커널 connect-redirect 컨트롤러

[English](./README.md)

이중 스택 WSK 프록시는 드라이버가 소유합니다. 컨트롤러에는 타입이 지정된 WFP
리디렉션 정책, 드라이버 카운터 조회, 수명 주기 IPC, 통계만 있습니다.

수락한 두 소켓은 중첩된 `io::with_async_transport` 소유자 안에서 실행하고,
두 릴레이 방향은 `kernel::join_bidirectional`로 결합합니다. 공급자와 스트림의
선언 순서 규칙이나 수동 콜백 drain 순서는 없습니다. 조기 반환, 취소, 어느
한 방향의 실패도 새 작업을 닫고 두 전송을 join한 뒤 소유 작업을 완료합니다.

```text
crtsys_wfp_kernel_connect_redirect_controller.exe <origin-port> <ipc-directory>
```

유효한 커널 리스너 포트를 조회하고 두 필터를 설치한 뒤
`controller.ready`를 만듭니다. `stop.request`가 오면 정책을 제거하고 최종 WSK
카운터와 불투명 리디렉션 레코드 캡처 수를 `controller.stats`에 기록합니다.

소켓 리스너, 클라이언트, 트래픽, PASS 판정은
`test/wfp/runtime/fixtures/kernel/connect-redirect`에 있습니다. 제품 프로젝트를
빌드하면 컨트롤러 옆에 `crtsys_wfp_kernel_connect_redirect_acceptance.exe`도
생성됩니다.
