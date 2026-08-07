# ALE connect-block 정책 controller

[English](./README.md)

드라이버가 typed ALE authorization을 수행합니다. controller는 정책과 health
작업만 소유하며 listener, client, traffic generator, PASS assertion,
self-test를 포함하지 않습니다.

```text
crtsys_wfp_ale_connect_block_controller.exe --serve <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --persistent-install|--persistent-check|--persistent-migrate|--persistent-rollback|--persistent-recover|--persistent-uninstall <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --arbitration <port> <application.exe> <ipc-directory>
```

persistent mode는 revision 전환을 각각 명시적인 child process 작업으로
공개합니다. arbitration은 `enable-block`, `block.ready`, `disable-block`,
`recovered.ready` 명령을 사용합니다. 모든 모드는 `controller.ready`를
공개하고 `stop.request`를 기다린 뒤 `controller.stats`를 기록합니다.

연결 probe와 lifecycle assertion은 모두
`test/wfp/runtime/fixtures/kernel/ale-connect-block`에 있습니다. 제품 프로젝트를
빌드하면 controller 옆에 `crtsys_wfp_ale_connect_block_acceptance.exe`도
생성됩니다.
