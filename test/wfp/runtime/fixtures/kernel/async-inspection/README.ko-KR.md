# Async-inspection acceptance 테스트

이 fixture가 listener, client connect, 시간 검증, PASS 출력 및
unload-race fan-out을 모두 소유합니다. 제품 controller를 실행하고
`controller.ready`, 드라이버 중지/재시작 확인 응답, `release-policy`,
`policy.released`, `stop.request`, `controller.stats` IPC로 조율합니다. race는
pending decision drain, fail-closed cancellation, driver reload, 정책 cleanup을
검증합니다. WFP 정책을 직접 설치하거나 드라이버를 열지 않습니다.

```text
crtsys_wfp_async_inspection_acceptance.exe <controller.exe> <ipc-directory>
crtsys_wfp_async_inspection_acceptance.exe <controller.exe> <ipc-directory> --unload-race <connection-count> <driver-service>
```
