# KMDF Verifier 스트레스 테스트

저장소 테스트와 공개 예제의 구분은 [KMDF 테스트 색인](../README.ko-KR.md)을 참고하십시오.

이 문서는 온보딩 예제가 아니라 테스트 fixture입니다. 다음을 의도적으로 함께 검사합니다.

- 동시 open, close, transform IOCTL 연산
- 보류 manual-queue request에서 release와 cancel의 경쟁
- 정확히 한 번의 완료 및 counter 검사
- KMDF 진행 보장 콜백
- VM runner에서 반복되는 service load, app 실행, unload, delete

모든 비동기 연산에는 제한된 대기가 있습니다. release/cancel 경쟁은 어느 쪽이 이겨도 되지만 request는 정확히 한 번 완료되어야 하며, 그에 대응하는 driver counter만 증가해야 합니다.

## 빌드

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-verifier-stress `
  -Architecture x64 `
  -Configuration Release
```

app은 선택적으로 iteration과 worker 수를 받습니다.

```text
crtsys_kmdf_verifier_stress_app.exe [iterations] [workers]
```

기본값은 race iteration 64회와 병렬 transform worker 4개입니다.

## Driver Verifier 절차

이 fixture는 kernel debugger가 연결된 폐기 가능한 테스트 VM에서만 실행하십시오. `crtsys_kmdf_verifier_stress.sys`에 표준 Driver Verifier 설정을 활성화하고 guest를 재부팅한 뒤 app을 실행하기 전에 `verifier /query`로 target이 나열되는지 확인하십시오. 대표적인 실행은 iteration 64회, worker 4개, 독립적인 driver load/unload cycle 3회를 사용합니다.

모든 app 호출과 service 전환이 verifier breakpoint 또는 bugcheck 없이 통과할 때만 테스트가 성공입니다. 실행 후 `verifier /query`를 기록하여 load/unload 및 Special Pool counter가 실제로 검증되었음을 보여야 합니다. 테스트 뒤에는 guest의 이전 Verifier 설정을 복원하고 재부팅하십시오.

저장소 안의 [`Run-KmdfVmAcceptance.ps1`](../runtime/Run-KmdfVmAcceptance.ps1)는 이 fixture를 staging하고 `StressIterations`, `StressWorkers`, `StressLoadCycles`로 iteration, worker, load-cycle 수를 제어합니다. 전체 폐기 가능 VM 절차는 [runtime fixture guide](../runtime/README.ko-KR.md)를 참고하십시오.
