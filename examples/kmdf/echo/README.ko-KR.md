# NTL KMDF 에코 및 취소 예제

[English](./README.md)

루트 열거 PnP 드라이버가 순차 KMDF 큐에 지연 에코 요청 하나를 보관합니다.
one-shot WDF 타이머가 정상 요청을 완료하고 취소 콜백이 `CancelIoEx`를
처리합니다. `try_mark_cancelable()`/`try_unmark_cancelable()` 경쟁에서
취소가 이기면 취소 콜백만 요청을 완료합니다.

앱은 짧은 요청의 타이머 완료, 긴 요청의 `ERROR_OPERATION_ABORTED`, 이후
요청을 통한 큐 복구를 확인합니다.

```powershell
cmake -S examples\kmdf\echo -B artifacts\examples\kmdf-echo -A x64
cmake --build artifacts\examples\kmdf-echo --config Debug
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_echo_ntl_sample_app.exe
.\remove.ps1
```

개발 드라이버는 폐기 가능한 테스트 VM에서만 설치하십시오.
