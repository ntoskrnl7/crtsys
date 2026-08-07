# NTL KMDF filter-stack 예제

[English](./README.md)

이 software-only 예제는 root-enumerated KMDF function driver와 NTL KMDF upper
filter를 같은 device stack에 설치합니다.

function driver는 device interface를 게시하고 형식화된 query IOCTL을 처리합니다.
filter는 `device_init::filter()`를 호출하고, 받은 request를 현재 stack type으로
format하고, 형식화된 completion callback을 등록한 뒤 request를
`device::default_io_target()`으로 보냅니다. target은 입력에 1을 더하고 filter
completion은 10을 더한 뒤 자신의 layer를 기록합니다. 앱은 두 변환과 관찰 가능한
두 layer bit를 모두 요구하므로 target만 설치된 상태가 잘못 통과할 수 없습니다.

알 수 없는 PnP 및 power traffic은 framework가 계속 forward합니다. 이 예제는 raw
IRP preprocessing을 사용하지 않으며 device-class protocol을 구현한다고 주장하지
않습니다.

## 빌드

`crtsys_kmdf_filter_stack_sample_vs.sln`을 열거나 CMake를 사용합니다.

```powershell
cmake -S examples\kmdf\filter-stack `
      -B artifacts\examples\kmdf-filter-stack -A x64
cmake --build artifacts\examples\kmdf-filter-stack --config Debug
```

## 폐기 가능한 VM smoke test

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_filter_stack_app.exe
.\remove.ps1
```

예상 출력은 `NTL KMDF filter stack ok`로 시작하고 `layers=0x3`을 보고합니다.
개발용 드라이버는 폐기 가능한 테스트 VM에만 설치하세요.
