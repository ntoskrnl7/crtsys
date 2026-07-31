# NTL KMDF WMI 예제

[English](./README.md)

MOF 기반 데이터 공급자의 조회, 전체 instance 설정, 항목 설정과 메서드 콜백,
이벤트 전용 공급자와 사용자 모드 이벤트 구독을 끝까지 보여주는 PnP
드라이버입니다. 장치와 instance의 C++ 컨텍스트는 NTL이 관리합니다.

```powershell
cmake -S examples\kmdf\wmi -B artifacts\examples\kmdf-wmi -A x64
cmake --build artifacts\examples\kmdf-wmi --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\crtsys_kmdf_wmi_ntl_sample_app.exe
.\remove.ps1
```

앱은 WMI instance 조회·수정, `Transform` 호출, 이벤트 구독과 IOCTL 기반
이벤트 발생을 검증합니다. WMI 콜백은 KMDF 계약에 따라 `PASSIVE_LEVEL`에서
실행됩니다. WMI provider와 instance의 execution level은 부모에게서
상속하도록 유지해야 합니다.
