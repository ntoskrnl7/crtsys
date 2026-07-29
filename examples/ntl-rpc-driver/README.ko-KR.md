# NTL RPC 예제 드라이버

[English](./README.md)

공유 callback 선언 하나에서 커널 RPC 서버와 사용자 모드 client를 생성하는
예제입니다. 형식화된 동기·비동기 호출, `OVERLAPPED`, timeout과 `CancelIoEx`,
협력적 커널 취소, 계약 버전·기능 검색, 호출자 권한 검사, reliable 알림,
재연결 replay, backpressure가 있는 형식화된 stream, C++20 coroutine과
`stop_token`을 다룹니다.

## 빌드와 로드

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64
cmake --build examples\ntl-rpc-driver\build_x64 --config Debug
sc create CrtSysNtlRpcSample binpath= "C:\path\to\crtsys_ntl_rpc_sample.sys" type= kernel start= demand
sc start CrtSysNtlRpcSample
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe 21 7
sc stop CrtSysNtlRpcSample
sc delete CrtSysNtlRpcSample
```

## 구조와 동작

계약은 [`shared/ntl_rpc_sample.hpp`](./shared/ntl_rpc_sample.hpp), 자료형은
`shared/ntl_rpc_sample_types.hpp`, 커널 구현은 `driver/`, 앱 호출 예제는
`app/`에 있습니다. 앱은 첫 호출 전에 `require_contract()`로 버전과 method
호환성을 확인합니다.

`async_call<T>`는 요청·응답 buffer, event, `OVERLAPPED`와 device handle을
소유하며 `wait()`, `get()`, `cancel()`을 제공합니다. 서버는 asynchronous
endpoint의 PASSIVE_LEVEL work item에서 실행되고 `call_context`로 취소를
관찰합니다. C++20에서는 같은 객체를 `co_await`하거나 `stop_token`으로
취소할 수 있습니다.

reliable 알림과 stream record는 ACK 전까지 보존되며 제한된 큐와 replay
sequence를 사용합니다. 앱은 알림 재연결, 단일·batch stream, 명시적 취소,
coroutine과 stop-token 경로를 순서대로 검증합니다. 성공 시 마지막에
`all RPC examples completed`를 출력합니다.
