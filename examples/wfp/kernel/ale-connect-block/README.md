# ALE connect-block policy controller

[Korean](./README.ko-KR.md)

The driver performs typed ALE authorization. The controller owns policy and
health operations only; it contains no listener, client, traffic generator,
PASS assertion, or self-test.

```text
crtsys_wfp_ale_connect_block_controller.exe --serve <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --persistent-install|--persistent-check|--persistent-migrate|--persistent-rollback|--persistent-recover|--persistent-uninstall <port> <ipc-directory>
crtsys_wfp_ale_connect_block_controller.exe --arbitration <port> <application.exe> <ipc-directory>
```

The persistent modes expose each revision transition as an explicit child
process operation. Arbitration uses `enable-block`, `block.ready`,
`disable-block`, and `recovered.ready` commands. Every mode publishes
`controller.ready`, waits for `stop.request`, and writes `controller.stats`.

All connectivity probes and lifecycle assertions are in
`test/wfp/runtime/fixtures/kernel/ale-connect-block`. The product build emits
`crtsys_wfp_ale_connect_block_acceptance.exe` next to the controller.
