# 비동기 ALE 검사 controller

[English](./README.md)

드라이버는 ALE authorization을 pend하고 PASSIVE_LEVEL worker에서 완료합니다.
controller에는 typed 정책 설치와 lifecycle IPC만 있습니다.

```text
crtsys_wfp_async_inspection_controller.exe --serve <permit-v4> <block-v4> <permit-v6> <block-v6> <ipc-directory>
crtsys_wfp_async_inspection_controller.exe --unload-race <permit-v4> <block-v4> <permit-v6> <block-v6> <driver-service> <ipc-directory>
```

`--serve`는 `stop.request`까지 정책을 유지합니다. `--unload-race`에서는
fixture가 `stop-driver`/`driver.stopped`, `start-driver`/`driver.started`,
`release-policy`/`policy.released` handshake를 구동합니다. controller가 SCM과
정책 작업만 소유하며 트래픽 생성이나 결과 assertion은 하지 않습니다. port와
상태는 `controller.ready`, `controller.stats`로 공개합니다.

listener, client traffic, 시간/PASS 검사, race fan-out은
`test/wfp/runtime/fixtures/kernel/async-inspection`에 있습니다. 제품 프로젝트를
빌드하면 controller 옆에 `crtsys_wfp_async_inspection_acceptance.exe`도 생성됩니다.
