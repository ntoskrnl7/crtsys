# ALE connect-block 정책 컨트롤러

[English](./README.md)

드라이버가 타입이 지정된 ALE 권한 확인을 수행합니다. 컨트롤러는 정책과 상태 확인
작업만 소유하며 리스너, 클라이언트, 트래픽 생성기, PASS 판정,
자체 테스트는 포함하지 않습니다.

```text
crtsys_wfp_ale_connect_block_controller.exe --serve <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --persistent-install|--persistent-check|--persistent-migrate|--persistent-rollback|--persistent-recover|--persistent-uninstall <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --arbitration <port> <application.exe> <ipc-directory>
```

영구 모드는 리비전 전환을 각각 명시적인 자식 프로세스 작업으로
제공합니다. 중재 모드는 `enable-block`, `block.ready`, `disable-block`,
`recovered.ready` 명령을 사용합니다. 모든 모드는 `controller.ready`를
게시하고 `stop.request`를 기다린 뒤 `controller.stats`를 기록합니다.

연결 프로브와 수명 주기 판정은 모두
`test/wfp/runtime/fixtures/kernel/ale-connect-block`에 있습니다. 제품 프로젝트를
빌드하면 컨트롤러 옆에 `crtsys_wfp_ale_connect_block_acceptance.exe`도
생성됩니다.
