# NTL KMDF PnP 예제

[English](./README.md)

이 예제는 PnP 및 power lifecycle 전반에서 형식화된 NTL wrapper를 사용하는
root-enumerated KMDF device입니다.

- prepare/release-hardware callback 및 resource-list 순회
- D0 entry/exit 상태
- device-interface 등록 및 SetupAPI를 통한 사용자 모드 검색
- idle-policy 구성
- 형식화된 입력/출력 buffer를 사용하는 PASSIVE-level sequential queue
- 동반 앱이 검증하는 context 소유 counter

포함된 INF는 개발 전용 hardware ID `Root\CrtSysKmdfNtlPnpSample`을 사용합니다.

## 빌드

`crtsys_kmdf_pnp_ntl_sample_vs.sln`을 열어 `Debug|x64` 또는 `Release|x64`로
빌드하거나 저장소 root에서 CMake를 사용합니다.

```powershell
cmake -S examples\kmdf\pnp `
      -B artifacts\examples\kmdf-pnp -A x64
cmake --build artifacts\examples\kmdf-pnp --config Debug
```

## 폐기 가능한 VM smoke test

관리자 PowerShell session에서 빌드한 package를 설치합니다.

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_pnp_ntl_sample_app.exe 40
.\remove.ps1
```

앱은 device interface를 찾고 형식화된 query IOCTL을 보낸 뒤 prepare-hardware와
D0-entry callback이 한 번 이상 실행됐는지 확인합니다. 개발용 드라이버는 폐기
가능한 테스트 VM에만 설치하세요.
