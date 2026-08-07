# NTL KMDF echo 및 취소 예제

[English](./README.md)

이 root-enumerated PnP 드라이버는 지연 echo request 하나를 sequential KMDF queue에
보관합니다. one-shot WDF timer가 request를 완료하며 request의 취소 callback은
`CancelIoEx`를 처리합니다.

queue는 `WdfSynchronizationScopeQueue`를 사용하고 timer는 automatic
serialization을 적용해 해당 queue를 parent로 둡니다. 따라서 queue dispatch,
timer expiry 및 request cancellation이 framework queue lock을 공유합니다. source는
필수 `try_mark_cancelable()` / `try_unmark_cancelable()` handshake도 수행합니다.
취소가 경쟁에서 이기면 `try_unmark_cancelable()`이 `STATUS_CANCELLED`를 반환하고
cancel callback만 request를 완료합니다.

앱은 관찰 가능한 세 가지 조건을 검증합니다.

1. 짧은 request가 timer에서 완료됩니다.
2. 긴 request가 취소되어 `ERROR_OPERATION_ABORTED`를 보고합니다.
3. 이후 request로 queue가 복구됐음을 입증하고 완료 및 취소 counter를 모두
   보고합니다.

## 빌드

`crtsys_kmdf_echo_ntl_sample_vs.sln`을 열거나 CMake로 빌드합니다.

```powershell
cmake -S examples\kmdf\echo `
      -B artifacts\examples\kmdf-echo -A x64
cmake --build artifacts\examples\kmdf-echo --config Debug
```

## 폐기 가능한 VM smoke test

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_echo_ntl_sample_app.exe
.\remove.ps1
```

예상 앱 출력은 `NTL KMDF echo ok`로 시작합니다. 새 device에서는 완료된 request
두 개와 취소된 request 하나를 보고합니다. 반복 실행하면 첫 echo 이후 완료와 취소
counter가 각각 하나씩 증가하는지 검증합니다. 개발용 드라이버는 폐기 가능한 테스트
VM에만 설치하세요.
