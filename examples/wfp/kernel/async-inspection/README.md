# Async ALE inspection controller

[Korean](./README.ko-KR.md)

The driver pends ALE authorization and completes it on a PASSIVE_LEVEL worker.
The controller contains only typed policy installation and lifecycle IPC.

```text
crtsys_wfp_async_inspection_controller.exe --serve <permit-v4> <block-v4> <permit-v6> <block-v6> <ipc-directory>
crtsys_wfp_async_inspection_controller.exe --unload-race <permit-v4> <block-v4> <permit-v6> <block-v6> <driver-service> <ipc-directory>
```

`--serve` keeps policy until `stop.request`. In `--unload-race`, the fixture
drives `stop-driver`/`driver.stopped`, `start-driver`/`driver.started`, and
`release-policy`/`policy.released` handshakes. The controller owns the SCM and
policy operations; it does not generate traffic or assert results. Ports and
state are published through `controller.ready` and `controller.stats`.

Listeners, client traffic, timing/PASS checks, and race fan-out live under
`test/wfp/runtime/fixtures/kernel/async-inspection`. The product build emits
`crtsys_wfp_async_inspection_acceptance.exe` next to the controller.
