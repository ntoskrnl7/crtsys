# NTL 미니필터 작업 로그 예제

[English](./README.md)

WDK **MiniSpy**의 작고 읽기 쉬운 대응 예제입니다.

```text
형식화된 I/O 콜백 → 크기가 제한된 nonpaged 큐 → 형식화된 port RPC → 콘솔
```

`.ntlspy` 파일의 create/read/write/cleanup만 기록합니다. 앱은 파일을 만들고
쓰고 읽은 뒤 닫고 큐를 비웁니다. `CLEANUP`은 마지막 handle이 닫힐 때,
`CLOSE`는 마지막 file-object 참조가 해제될 때 도착하므로 앱은
`CloseHandle` 직후의 `CLOSE`를 필수 조건으로 삼지 않습니다.

```powershell
cmake -S examples\minifilter\operation-log -B out\minifilter-operation-log-x64 -A x64
cmake --build out\minifilter-operation-log-x64 --config Debug
```

테스트 서명한 드라이버를 VM에 설치한 뒤
`crtsys_minifilter_operation_log_sample_app.exe`를 관리자 권한으로 실행합니다.
overflow와 순서, unload 중 drain 검증은 `test/flt/runtime`에 있습니다.
