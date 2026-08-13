# 비동기 ALE 검사 컨트롤러

[English](./README.md)

드라이버는 ALE 권한 확인을 보류하고 PASSIVE_LEVEL 작업자에서 완료합니다.
컨트롤러에는 타입이 지정된 정책 설치와 수명 주기 IPC만 있습니다.

```text
crtsys_wfp_async_inspection_controller.exe --serve <permit-v4> <block-v4> <permit-v6> <block-v6> <ipc-directory>
crtsys_wfp_async_inspection_controller.exe --unload-race <permit-v4> <block-v4> <permit-v6> <block-v6> <driver-service> <ipc-directory>
```

`--serve`는 `stop.request`까지 정책을 유지합니다. `--unload-race`에서는
픽스처가 `stop-driver`/`driver.stopped`, `start-driver`/`driver.started`,
`release-policy`/`policy.released` 핸드셰이크를 구동합니다. 컨트롤러가 SCM과
정책 작업만 소유하며 트래픽 생성이나 결과 판정은 하지 않습니다. 포트와
상태는 `controller.ready`, `controller.stats`로 게시합니다.

리스너, 클라이언트 트래픽, 시간/PASS 검사, 경합 fan-out은
`test/wfp/runtime/fixtures/kernel/async-inspection`에 있습니다. 제품 프로젝트를
빌드하면 컨트롤러 옆에 `crtsys_wfp_async_inspection_acceptance.exe`도 생성됩니다.
