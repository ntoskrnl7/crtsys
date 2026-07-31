# NTL KMDF 드라이버 예제

[English](./README.md)

`crtsys`의 커널 호환 CRT/STL과 `ntl/kmdf/` 래퍼를 사용하는 비 PnP KMDF
제어 드라이버입니다. WDF가 드라이버, 장치, 큐, 요청, 전원 및 객체 수명을
계속 소유합니다.

이 예제는 형식화된 IOCTL, 장치·파일 컨텍스트, 수동 큐와 overlapped I/O,
`CancelIoEx`, forward-progress 예약 요청, work item, passive timer, DPC,
잠금, lookaside 메모리와 여러 WDF 객체 도우미를 한 실행 경로에서 검증합니다.
WDF 콜백 경계에서는 모든 C++ 예외를 잡습니다.

## 빌드

`crtsys_kmdf_ntl_sample_vs.sln`을 열어 `Debug|x64` 또는 `Release|x64`로
빌드하거나 다음을 실행합니다.

```powershell
cmake -S examples\kmdf\basic -B artifacts\examples\kmdf-basic -A x64
cmake --build artifacts\examples\kmdf-basic --config Debug
```

## VM 스모크 테스트

```bat
sc create CrtSysKmdfNtlSample binpath= "C:\path\crtsys_kmdf_ntl_sample.sys" type= kernel start= demand
sc start CrtSysKmdfNtlSample
crtsys_kmdf_ntl_sample_app.exe 36
sc stop CrtSysKmdfNtlSample
sc delete CrtSysKmdfNtlSample
```

성공 시 `NTL KMDF ok`와 `NTL KMDF manual queue ok`가 출력됩니다.
