# NTL KMDF reference 드라이버

[English](./README.md)

이 project는 제품 지향 software KMDF device의 권장 시작점입니다. 작은 교육용
예제에 의도적으로 나누어 둔 다음 공통 규칙을 하나로 결합합니다.

- root-enumerated PnP device와 device interface
- `PrepareHardware`, release, D0 entry 및 D0 exit 상태
- WDF가 소유하는 device, queue 및 file별 C++ context
- 고정 폭의 versioned 사용자/커널 ABI
- 엄격한 입력·출력 크기 검증
- sequential passive queue와 one-shot passive timer
- 정확히 한 번 완료되는 `mark cancelable` / `unmark cancelable` 경쟁
- 관찰 가능한 session, lifecycle, completion, cancellation 및 IRQL 값

드라이버는 request를 pending 상태로 만들기 전에 buffered operation의 snapshot을
저장합니다. `METHOD_BUFFERED`에서는 입력과 출력이 같은 system buffer를 alias할 수
있으므로 query 경로는 더 큰 출력 구조를 지우기 전에 입력 header도 검증하고
복사합니다.

앱은 독립된 file session 두 개를 열고 ABI와 PnP/power 상태를 검증하며, 지연
operation 하나를 완료하고 다른 하나를 취소한 뒤 session 하나를 닫고 최종 counter를
검증합니다. 성공 marker는 `NTL KMDF reference ok`로 시작합니다.

이는 software-device reference이지 simulation hardware driver가 아닙니다. 실제
제품에서는 root hardware ID, interface GUID, IOCTL contract 및 transform operation을
교체하세요. PCI, USB, DMA, interrupt, firmware 및 class-extension 계약에는 여전히
실제 bus나 device가 필요합니다.

## 빌드

`crtsys_kmdf_reference_vs.sln`을 열거나 CMake를 사용합니다.

```powershell
cmake -S examples\kmdf\reference `
      -B artifacts\examples\kmdf-reference -A x64
cmake --build artifacts\examples\kmdf-reference --config Debug
```

## 폐기 가능한 VM 검증

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_reference_app.exe
.\remove.ps1
```

변경 사항을 배포 준비 완료로 판단하기 전에 저장소의 KMDF VM acceptance gate를
사용하세요. 이 gate는 x64와 WOW64 client로 앱을 반복 실행하고, device를 restart하고,
선택한 드라이버를 Driver Verifier 아래에서 실행하고, 새 bugcheck와 dump를 검사하고,
root device를 제거한 뒤 gate에 전달된 기존 Verifier configuration을 복원합니다.
