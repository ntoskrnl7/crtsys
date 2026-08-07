# 사용자 모드 coroutine socket

[NTL 문서로 돌아가기](./README.ko-KR.md)

`<ntl/net/io/async_socket>`은 overlapped Winsock I/O용 C++20 사용자 모드 adapter입니다. socket을 하나의 I/O completion port에 연결하고 completion worker에서 일시 중단된 coroutine을 재개합니다. WFP controller 또는 로컬 proxy data plane에 적합하며 kernel mode에서는 사용할 수 없습니다.

Winsock process 초기화는 호출자가 계속 소유합니다.

```cpp
WSADATA data{};
if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
  throw std::runtime_error("WSAStartup failed");
```

`WSA_FLAG_OVERLAPPED`로 socket을 만든 뒤 소유권을 `async_socket`에 넘깁니다.

```cpp
ntl::net::io_completion_context io;

SOCKET native = WSASocketW(
    AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
    WSA_FLAG_OVERLAPPED);

ntl::net::async_socket connection(io, native);
```

생성은 socket을 `io`에 연결합니다. 연결에 실패하면 생성자는 전달받은 socket을 닫고 `std::system_error`를 던집니다.

## 하나의 completion context 공유

하나의 `io_completion_context`는 연결된 socket 여러 개를 소유할 수 있습니다. proxy는 수락한 connection마다 completion port 또는 OS thread를 만들 필요가 없습니다. context 하나를 유지하고 socket 쌍마다 소유 coroutine task를 만들며 이 task를 connection registry에 보관하십시오.

registry는 task 완료와 일시적인 IOCP 유휴 상태를 구분해야 합니다. shutdown 중에는 먼저 모든 connection에 cancellation을 요청하고, 모든 소유 task가 끝날 때까지 기다린 다음 공유 context를 파괴하기 전에 `wait_for_idle()`을 호출합니다. [`browser-https-inspection` sample](../../examples/wfp/user/browser-https-inspection)은 이 모델을 사용하며 relay contract test는 하나의 context에서 동시에 실행되는 많은 socket 쌍을 검사합니다.

## Coroutine 연산

socket은 awaiter 세 개를 제공합니다.

```cpp
auto count = co_await connection.read_some_borrowed(buffer);
auto exact = co_await connection.read_exactly_borrowed(message);
auto sent = co_await connection.write_all(message);
```

- `read_some_borrowed()`는 한 번의 receive 뒤 완료하고 정상 EOF에는 0을 반환합니다.
- `read_exactly_borrowed()`는 span이 찰 때까지 부분 receive를 다시 제출합니다. await가 끝날 때까지 destination span은 살아 있어야 합니다. 그 전에 정상 EOF가 오면 `std::system_error(ERROR_HANDLE_EOF, ...)`를 던집니다.
- `write_all()`은 전체 span이 수락될 때까지 부분 send를 다시 제출합니다.

모든 연산은 별도의 `OVERLAPPED`를 사용합니다. `await_suspend()`와 경합하는 completion은 frame을 두 번 재개할 수 없습니다. submitter와 completion worker가 atomic submitting/suspended/completed 상태를 교환합니다. 즉시 완료도 IOCP를 통해 도착하며 continuation은 blocking receive thread가 아니라 IOCP worker에서 재개됩니다.

C++ 표준에는 최상위 `task<T>`가 없으므로 이 header는 이를 강제하지 않습니다. coroutine frame은 application이 소유합니다. stream-edit sample은 frame 밖에 result를 두는 event 기반 self-destroying example task를 사용합니다.

## 수명과 취소

- destination/source span은 `await_resume()`까지 유효해야 합니다.
- 각 socket과 pending operation은 IOCP runtime state를 유지합니다. 따라서 `io_completion_context` facade는 member 선언 순서와 무관하게 child보다 먼저 close/소멸할 수 있습니다.
- `async_socket::cancel()`은 socket에 대해 `CancelIoEx`를 호출합니다. 선택한 read/write 하나가 아니라 해당 socket의 모든 pending operation을 취소합니다.
- socket을 닫아도 pending overlapped operation은 완료됩니다.
- `io_completion_context::close()`는 idempotent이며 새 child를 거부하고 제출된 operation을 기다린 뒤 native completion port를 해제하기 전에 worker를 join합니다. completion continuation 안에서 호출할 수 있으며 worker는 callback이 반환할 때까지 마지막 runtime reference를 유지합니다.
- `wait_for_idle()`은 현재 제출된 OS operation이 완료되었음을 뜻합니다. 재개된 coroutine은 다른 operation을 제출할 수 있으므로 task 완료가 권위 있는 작업 종료 신호입니다.

Winsock/IOCP 오류는 Windows system category를 사용하는 `std::system_error`로 전달됩니다.

## WFP 경계

이것은 사용자 모드 transport primitive입니다. policy 설치, HTTP parsing, 원래 redirect destination 복구, redirect recursion 방지는 수행하지 않습니다. `<ntl/net/tls/stream>`은 그 위에 구축된 별도의 Schannel TLS 계층입니다. WFP connect redirect와 application framing은 별도 관심사로 유지됩니다. [사용자 모드 Schannel TLS stream](./tls-stream.ko-KR.md)을 참고하십시오.

[`stream-edit` controller](../../examples/wfp/kernel/stream-edit)는 런타임 증거입니다. 실제 loopback client/server 경로는 kernel callout이 두 write에 걸쳐 분할된 token을 교체하는 동안 `co_await read_exactly_borrowed()`, `co_await write_all()`을 사용합니다. `--coroutine-self-test` 모드는 WFP policy 없이도 fragmented loopback transfer, `CancelIoEx`, 불완전 read의 EOF 동작을 검증합니다.
