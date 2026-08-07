# NTL RPC 비동기 전송 테스트

이 fixture는 선택적 `server_options::asynchronous()` endpoint 모드와 사용자 모드 `client::invoke_async()` 요청 소유자를 검증합니다. 다음을 다룹니다.

- 요청을 시작한 `client` 객체보다 오래 유지되는 RPC 요청
- 살아 있는 I/O buffer를 해제하지 않고 timeout을 보고하는 제한된 대기
- 대상 지정 `CancelIoEx` 취소
- `ntl::rpc::call_context`를 통한 실행 중 callback의 조기 종료
- 형식화된 값 및 void 결과
- 보류 요청 객체가 범위를 벗어날 때의 안전한 취소 및 drain
- 하나의 장치 handle에서 동시에 실행되는 여러 overlapped 호출
- callback 호출 전 초과 작업을 거부하는 구성된 pending-call 한도
- 진행 중인 비동기 callback과 보류 IRP가 drain될 때까지 기다리는 service 중지
- service 재시작 후 새 contract query 및 RPC 호출

server는 `PASSIVE_LEVEL` 시스템 worker에서 application callback을 실행합니다. 취소 요청은 가능하면 queue에 든 callback이 시작되지 않게 합니다. callback이 이미 실행 중이면 NTL은 커널 실행을 종료하지 않습니다. callback이 반환할 때까지 기다린 뒤 출력을 버리고 `STATUS_CANCELLED`로 IRP를 완료합니다.

`ntl::rpc::call_context`를 받는 callback은 제한된 작업 단위 사이에서 `cancelled()`를 확인하거나 `throw_if_cancelled()`를 호출할 수 있습니다. 첫 번째 매개변수가 없는 기존 callback은 이전의 완료까지 drain하는 동작을 유지합니다.

이 문서는 온보딩 예제가 아니라 테스트 fixture입니다. 보류 IRP 또는 취소 소유권 규칙을 변경하기 전에 폐기 가능한 커널 디버깅 VM에서만 실행하고 Driver Verifier로 반복 실행하십시오.

진행 중인 중지 및 재시작 경로를 시험하려면 설치된 service 이름을 전달하십시오.

```text
crtsys_rpc_async_app.exe CrtSysRpcAsync
```

이 fixture는 v143 x64 Debug driver와 v143 x64 Debug client, v142 x86 Debug client 조합에서 표준 Driver Verifier로 검증했습니다. 각 client는 진행 중인 service 중지와 재시작을 매 cycle에 포함하여 세 번의 외부 load cycle을 완료했습니다. 결합 실행에서는 태그 없는, 추적되지 않은 또는 실패한 pool allocation 없이 검증된 driver load 12회와 unload 12회를 기록했습니다.
