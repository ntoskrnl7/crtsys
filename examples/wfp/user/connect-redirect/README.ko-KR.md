# 사용자 모드 connect redirect proxy service

[English](./README.md)

이 제품 예제에는 WFP redirect 드라이버와 실제 dual-stack TCP proxy service만
있습니다. service가 proxy listener, typed redirect 정책, 원래 목적지 복구,
coroutine relay, lifecycle IPC, 통계를 소유합니다.

```text
crtsys_wfp_connect_redirect_proxy_service.exe <origin-port-v4> <origin-port-v6> <ipc-directory>
crtsys_wfp_connect_redirect_proxy_service.exe --unavailable-proxy <origin-port-v4> <origin-port-v6> <ipc-directory>
```

두 proxy listener와 WFP filter가 활성화되면 `controller.ready`를 만듭니다.
`stop.request`를 만들면 정책을 제거하고 relay를 drain하며 최종 byte counter를
`controller.stats`에 기록합니다.
`--unavailable-proxy`는 트래픽을 만들지 않고 닫힌 proxy endpoint를 대상으로
동일한 fail-closed 정책만 유지합니다. acceptance fixture가 IPC로 이 배포 상태를
구동하고 관찰합니다.

controlled origin, client, PASS assertion, 자동 트래픽은
`test/wfp/runtime/fixtures/user/connect-redirect`에 있습니다. 제품 프로젝트를
빌드하면 service 옆에 `crtsys_wfp_connect_redirect_acceptance.exe`도 생성되며,
fixture에는 WFP 정책 코드가 없습니다.

```powershell
cmake -S examples\wfp\user\connect-redirect `
      -B artifacts\examples\wfp-user-connect-redirect -A x64
cmake --build artifacts\examples\wfp-user-connect-redirect --config Debug
```
