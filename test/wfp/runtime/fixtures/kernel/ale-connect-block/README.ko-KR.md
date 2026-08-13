# ALE connect-block 허용성 테스트

이 픽스처가 리스너, 연결 프로브, 검증 및 PASS 출력을 담당합니다.
임시 적용, 영구 설치/상태 확인/마이그레이션/롤백/복구/제거 및 공급자
중재 컨트롤러 모드를 자식 프로세스로 실행하고 파일 IPC로 정책 전환을
조율합니다. `--crash-recovery`는 살아 있는 임시 컨트롤러를 종료한 뒤 동적 BFE
세션이 정책을 제거하여 연결을 복구하는지 검증합니다. 픽스처는 WFP를 직접 열지 않습니다.

```text
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory>
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --persistent-lifecycle
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --arbitration
crtsys_wfp_ale_connect_block_acceptance.exe <controller.exe> <ipc-directory> --crash-recovery
```
