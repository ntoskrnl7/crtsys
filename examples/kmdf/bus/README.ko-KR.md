# NTL KMDF bus 및 PDO 예제

[English](./README.md)

이 예제는 root-enumerated KMDF bus FDO와 child function driver가 형식화된 NTL
KMDF query interface로 통신하는 방법을 보여줍니다. bus driver는 다음 작업을
수행합니다.

- 형식화된 dynamic `child_list` 구성
- child-create callback에서 `ntl::kmdf::pdo_init` 수신
- device, instance, hardware, compatible 및 localized text data 할당
- 형식화된 resource-requirements와 boot-resource callback을 통해 virtual PDO의
  빈 logical resource configuration 보고
- 관리되는 C++ context storage를 포함한 `ntl::kmdf::pdo` 생성
- 형식화된 eject, lock, wake 및 reported-missing PDO callback 등록
- PDO에서 reference-counted driver-defined interface 노출
- PDO identification과 parent 관계 검증

child function driver는 `device::try_query_interface()`로 이 계약을 얻어 bus가
소유한 함수를 호출하고 사용자 모드 device interface를 게시합니다. 앱은 bus
management interface를 열고 `plug in -> query -> mark missing -> plug in -> eject`
흐름을 검증합니다. 여기에는 resource callback counter, SetupAPI arrival 및 child
function stack 제거도 포함됩니다.

## Visual Studio

`crtsys_kmdf_bus_ntl_sample_vs.sln`을 열고 `crtsys` package를 복원한 다음 x64
Debug 또는 Release configuration으로 빌드하세요. solution에는 bus driver, child
function driver 및 validation app이 있습니다. 저장소 개발 중에는
`examples/Directory.Build.props`가 저장소에서 생성한 local package를 선택합니다.

관리자 PowerShell에서 root-enumerated bus를 설치합니다.

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
```

그다음 앱을 실행합니다.

```powershell
.\x64\Debug\crtsys_kmdf_bus_ntl_sample_app.exe
```

다음 명령으로 제거합니다.

```powershell
.\remove.ps1
```

## CMake

```powershell
cmake -S examples\kmdf\bus `
      -B artifacts\examples\kmdf-bus -A x64
cmake --build artifacts\examples\kmdf-bus --config Debug
```
