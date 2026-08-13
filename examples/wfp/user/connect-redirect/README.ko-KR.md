# 사용자 모드 connect redirect 프록시 서비스

[English](./README.md)

이 제품 예제에는 WFP 리디렉션 드라이버와 실제 이중 스택 TCP 프록시 서비스만
있습니다. 서비스가 프록시 리스너, 타입이 지정된 리디렉션 정책, 원래 목적지 복구,
코루틴 릴레이, 수명 주기 IPC, 통계를 소유합니다.

```text
crtsys_wfp_connect_redirect_proxy_service.exe <origin-port-v4> <origin-port-v6> <ipc-directory>
crtsys_wfp_connect_redirect_proxy_service.exe --unavailable-proxy <origin-port-v4> <origin-port-v6> <ipc-directory>
```

두 프록시 리스너와 WFP 필터가 활성화되면 `controller.ready`를 만듭니다.
`stop.request`를 만들면 정책을 제거하고 릴레이를 drain하며 최종 바이트 카운터를
`controller.stats`에 기록합니다.
`--unavailable-proxy`는 트래픽을 만들지 않고 닫힌 proxy endpoint를 대상으로
동일한 실패 시 차단 정책만 유지합니다. 허용성 검사 픽스처가 IPC로 이 배포 상태를
구동하고 관찰합니다.

제어된 원본 서버, 클라이언트, PASS 판정, 자동 트래픽은
`test/wfp/runtime/fixtures/user/connect-redirect`에 있습니다. 제품 프로젝트를
빌드하면 서비스 옆에 `crtsys_wfp_connect_redirect_acceptance.exe`도 생성되며,
픽스처에는 WFP 정책 코드가 없습니다.

```powershell
cmake -S examples\wfp\user\connect-redirect `
      -B artifacts\examples\wfp-user-connect-redirect -A x64
cmake --build artifacts\examples\wfp-user-connect-redirect --config Debug
```
