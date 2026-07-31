# NTL KMDF 필터 스택 예제

[English](./README.md)

루트 열거 KMDF 함수 드라이버와 NTL KMDF 상위 필터를 같은 장치 스택에
설치하는 소프트웨어 전용 예제입니다. 대상 드라이버는 입력에 1을 더하고,
필터의 형식화된 완료 콜백은 10을 더합니다. 앱은 두 변환과 두 계층 비트를
모두 검사하므로 대상만 설치된 상태가 성공으로 오인되지 않습니다.

```powershell
cmake -S examples\kmdf\filter-stack -B artifacts\examples\kmdf-filter-stack -A x64
cmake --build artifacts\examples\kmdf-filter-stack --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_filter_stack_app.exe
.\remove.ps1
```

성공 출력에는 `NTL KMDF filter stack ok`와 `layers=0x3`이 포함됩니다.
