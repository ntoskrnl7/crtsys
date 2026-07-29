# NTL KMDF USB 드라이버 템플릿

[English](./README.md)

`EvtDevicePrepareHardware`에서 USB 장치를 만들고 첫 인터페이스의
bulk/interrupt 파이프를 열거하며, 첫 입력 파이프에 연속 판독기를 구성하는
템플릿입니다. 판독기는 D0 진입·이탈 때 시작·정지합니다.

INF의 `USB\VID_FFFF&PID_FFFF`는 자리표시자입니다. 실제 장치 ID와 endpoint
프로토콜에 맞게 바꾸기 전에는 설치하지 마십시오. 연속 판독 완료는 보통
`DISPATCH_LEVEL`에서 실행되므로 예제는 lock-free 원자 카운터만 사용합니다.

```powershell
cmake -S examples/kmdf/usb -B artifacts/examples/kmdf-usb -A x64
cmake --build artifacts/examples/kmdf-usb --config Debug
```

Visual Studio에서는 `crtsys_kmdf_usb_ntl_sample_vs.sln`을 사용합니다.
실행 검증에는 수정한 INF와 endpoint 계약에 일치하는 USB 장치가 필요합니다.
