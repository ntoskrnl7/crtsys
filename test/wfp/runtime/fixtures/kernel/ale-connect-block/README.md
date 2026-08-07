# ALE connect-block acceptance

This fixture owns the listener, connect probes, assertions, and PASS output.
It launches controller modes for ephemeral enforcement, persistent install /
health / migrate / rollback / recover / uninstall, and provider arbitration.
File IPC coordinates each policy transition. `--crash-recovery` terminates a
live ephemeral controller and proves that its dynamic BFE session removes
policy and restores connectivity. The fixture never opens WFP.

```text
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory>
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --persistent-lifecycle
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --arbitration
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --crash-recovery
```
