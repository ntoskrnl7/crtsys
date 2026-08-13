# KMDF 테스트

아래 디렉터리에는 compile, runtime, stress 중심의 KMDF fixture가 있습니다. [`examples/kmdf`](../../examples/kmdf/README.ko-KR.md)의 온보딩 예제와는 분리되어 있습니다.

Microsoft sample에서 타입이 지정된 NTL mechanism으로의 저장소 전체 매핑은 [WDK KMDF sample coverage matrix](WDK-SAMPLE-COVERAGE.ko-KR.md)에서 관리합니다.

| 테스트 | 목적 |
| --- | --- |
| [`compile`](compile) | 컴파일 타임 callback, object, request, context, PnP, filter, bus, DMA, USB, WMI contract |
| [`runtime`](runtime) | control, PnP, echo/cancel, reference ABI, bus, filter, WMI, restart, unload, cross-bitness, Driver Verifier, crash 검사, verifier 복원을 위한 소프트웨어 전용 build/staging 및 VM orchestration |
| [`verifier-stress`](verifier-stress/README.ko-KR.md) | Driver Verifier 실행을 위한 반복 queue, request, timer, work-item, object-lifetime, unload 검사 |

저장소가 사용하는 것과 같은 CI entry point를 통해 verifier fixture를 빌드하십시오.

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-verifier-stress `
  -Architecture x64 `
  -Configuration Release
```

저장소 안의 [`runtime/Run-KmdfVmAcceptance.ps1`](runtime/Run-KmdfVmAcceptance.ps1)는 build, staging, VM 실행, Driver Verifier 검사, 정리, 복원을 수행합니다. 매개변수와 acceptance criterion은 [runtime fixture guide](runtime/README.ko-KR.md)를 참고하십시오.
