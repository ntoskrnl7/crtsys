# NTL KMDF 버스 및 PDO 예제

[English](./README.md)

루트 열거 방식 KMDF 버스 FDO와 자식 함수 드라이버가 형식화된 NTL KMDF
query interface로 통신하는 예제입니다. 버스는 동적 `child_list`를 구성하고
`ntl::kmdf::pdo_init`으로 가상 PDO를 만든 뒤 식별 정보, 리소스 콜백,
꺼내기·잠금·절전 콜백과 참조 횟수 기반 인터페이스를 등록합니다.

앱은 `plug in → query → mark missing → plug in → eject` 흐름과 SetupAPI
도착·제거를 확인합니다.

```powershell
cmake -S examples\kmdf\bus -B artifacts\examples\kmdf-bus -A x64
cmake --build artifacts\examples\kmdf-bus --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_bus_ntl_sample_app.exe
.\remove.ps1
```

Visual Studio에서는 `crtsys_kmdf_bus_ntl_sample_vs.sln`을 사용합니다.
