# NTL KMDF PnP 예제

[English](./README.md)

루트 열거 KMDF 장치에서 prepare/release hardware, D0 진입·이탈, 리소스
순회, 장치 인터페이스, 유휴 정책과 형식화된 IOCTL을 보여줍니다. 포함된 INF는
개발용 하드웨어 ID `Root\CrtSysKmdfNtlPnpSample`을 사용합니다.

```powershell
cmake -S examples\kmdf\pnp -B artifacts\examples\kmdf-pnp -A x64
cmake --build artifacts\examples\kmdf-pnp --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_pnp_ntl_sample_app.exe 40
.\remove.ps1
```

앱은 SetupAPI로 인터페이스를 찾고 PnP 및 D0 콜백 실행을 확인합니다.
설치는 폐기 가능한 테스트 VM에서만 수행하십시오.
