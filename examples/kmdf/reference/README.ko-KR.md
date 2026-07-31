# NTL KMDF 참조 드라이버

[English](./README.md)

제품 지향 소프트웨어 KMDF 장치의 권장 시작점입니다. 루트 열거 PnP 장치,
장치 인터페이스, WDF 소유 C++ 컨텍스트, 고정 폭 버전 ABI, 엄격한 버퍼 크기
검사, passive 큐·타이머와 정확히 한 번 완료되는 취소 경쟁을 함께 다룹니다.

앱은 독립 파일 세션 두 개를 열어 ABI와 PnP/전원 상태를 검증하고, 지연 작업
하나는 완료하고 하나는 취소한 뒤 최종 카운터를 확인합니다. 실제 제품에서는
하드웨어 ID, 인터페이스 GUID, IOCTL 계약과 변환 동작을 교체해야 합니다.

```powershell
cmake -S examples\kmdf\reference -B artifacts\examples\kmdf-reference -A x64
cmake --build artifacts\examples\kmdf-reference --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_reference_app.exe
.\remove.ps1
```

배포 준비 여부를 판단하기 전 저장소의 KMDF VM acceptance gate와 Driver
Verifier 검증을 실행하십시오.
