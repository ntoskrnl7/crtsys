# NTL minifilter operation-log 예제

[English](./README.md)

이 예제는 WDK **MiniSpy** 예제의 작고 읽기 쉬운 대응 구현입니다. 전체 경로를 한
예제에서 확인할 수 있습니다.

`타입이 지정된 I/O 콜백 -> 크기가 제한된 nonpaged 큐 -> 타입이 지정된 포트 RPC -> 콘솔`

| WDK MiniSpy의 역할 | NTL 예제 표현 |
| --- | --- |
| `FLT_OPERATION_REGISTRATION` table | 타입이 지정된 `registration::on*` 호출 |
| 형식 없는 completion context | `completion_slot<completion_state>` |
| 열린 파일별 추적 | `stream_handle_context<tracked_handle>` |
| record list 및 동기화 | `record_queue` |
| command/reply protocol | 타입이 지정된 `ntl::rpc::method` descriptor |
| 필터 관리자 포트 | `driver::add_communication_port` |

일반 system activity가 예제 출력을 뒤덮지 않도록 `.ntlspy`로 끝나는 파일만
기록합니다. 앱은 이러한 파일 하나를 만들고 쓰고 읽은 뒤 handle을 닫고 queue를
비웁니다. create/read/write/cleanup은 반드시 요구하고 close가 이미 관찰됐는지는
별도로 보고합니다. `IRP_MJ_CLEANUP`은 마지막 handle이 닫힐 때 발생하지만
`IRP_MJ_CLOSE`는 file object의 마지막 참조가 해제된 후에만 발생합니다. 따라서
동기식 사용자 모드 테스트는 `CloseHandle` 직후에 close가 도착했다고 요구할 수
없습니다.

queue는 의도적으로 별도 파일에 있습니다. 이는 일반적인 커널 측 정책이며,
`driver/main.cpp`의 minifilter 전용 코드는 타입이 지정된 콜백과 소유권에만
집중합니다.

## 빌드

Visual Studio developer PowerShell에서 실행합니다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_operation_log_sample_vs.sln`을 열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_operation_log_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

폐기 가능한 VM에 test-signed 드라이버를 설치하고 로드한 다음
`crtsys_minifilter_operation_log_sample_app.exe`를 관리자 권한으로 실행하세요.

이 예제는 architecture를 보여주지만 MiniSpy의 모든 제품 기능을 구현하지는
않습니다. `test/flt/runtime`의 전체 runtime fixture는 overflow, sequence 순서 및
unload 시 queue drain도 검증합니다.
