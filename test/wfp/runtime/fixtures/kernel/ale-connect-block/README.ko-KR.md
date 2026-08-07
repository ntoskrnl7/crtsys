# ALE connect-block acceptance 테스트

이 fixture가 listener, connect probe, 검증 및 PASS 출력을 담당합니다.
임시 적용, 영구 install/health/migrate/rollback/recover/uninstall 및 provider
arbitration controller 모드를 child process로 실행하고
파일 IPC로 정책 전환을 조율합니다. `--crash-recovery`는 살아 있는 ephemeral
controller를 종료하고 dynamic BFE session이 정책을 제거해 연결을 복구하는지
검증합니다. fixture는 WFP를 직접 열지 않습니다.

```text
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory>
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --persistent-lifecycle
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --arbitration
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --crash-recovery
```
