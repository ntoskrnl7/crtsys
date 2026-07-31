# User-mode coroutine sockets

[Back to NTL documentation](./README.md)

`<ntl/net/io/async_socket>` is a C++20 user-mode adapter for overlapped Winsock I/O.
It associates sockets with one I/O completion port and resumes suspended
coroutines on the completion worker. It is suitable for a WFP controller or
local proxy data plane; it is not available in kernel mode.

The caller still owns Winsock process initialization:

```cpp
WSADATA data{};
if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
  throw std::runtime_error("WSAStartup failed");
```

Create sockets with `WSA_FLAG_OVERLAPPED`, then transfer ownership to
`async_socket`:

```cpp
ntl::net::io_completion_context io;

SOCKET native = WSASocketW(
    AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
    WSA_FLAG_OVERLAPPED);

ntl::net::async_socket connection(io, native);
```

Construction associates the socket with `io`. If association fails, the
constructor closes the transferred socket and throws `std::system_error`.

## Sharing one completion context

One `io_completion_context` can own many associated sockets. A proxy does not
need a completion port or operating-system thread per accepted connection:
keep one context alive, create an owning coroutine task for each socket pair,
and retain those tasks in a connection registry.

The registry must distinguish task completion from temporary IOCP idleness.
During shutdown it first requests cancellation for every connection, waits
until every owning task has completed, and then calls `wait_for_idle()` before
destroying the shared context. The
[`browser-https-inspection` sample](../../examples/wfp/browser-https-inspection)
uses this model and its relay contract test exercises many simultaneous socket
pairs on one context.

## Coroutine operations

The socket provides three awaiters:

```cpp
auto count = co_await connection.read_some(buffer);
auto exact = co_await connection.read_exactly(message);
auto sent = co_await connection.write_all(message);
```

- `read_some()` completes after one receive and returns zero for clean EOF.
- `read_exactly()` resubmits partial receives until the span is full. Clean EOF
  before that point throws `std::system_error(ERROR_HANDLE_EOF, ...)`.
- `write_all()` resubmits partial sends until the entire span is accepted.

All operations use a distinct `OVERLAPPED`. A completion that races
`await_suspend()` cannot resume the frame twice: the submitter and completion
worker exchange an atomic submitting/suspended/completed state. Immediate
completion still arrives through IOCP, and continuations resume on the IOCP
worker rather than a blocking receive thread.

The C++ standard does not provide a top-level `task<T>`, so this header does
not impose one. The application owns its coroutine frame. The stream-edit
sample uses an event-backed, self-destroying example task whose result lives
outside the frame.

## Lifetime and cancellation

- The destination/source span must remain valid until `await_resume()`.
- `io_completion_context` must outlive every associated socket, pending
  operation, and coroutine that can submit another operation.
- Do not destroy the context from its own completion worker.
- `async_socket::cancel()` calls `CancelIoEx` for the socket. This cancels all
  pending operations on that socket, not one selected read or write.
- Closing a socket also causes its pending overlapped operations to complete.
- Destroy sockets or request cancellation before destroying the context. The
  context waits for its submitted operations, posts a shutdown packet, joins
  its worker, and only then closes the completion port.
- `wait_for_idle()` means that currently submitted OS operations completed. A
  resumed coroutine may submit another operation, so task completion remains
  the authoritative end-of-work signal.

Winsock and IOCP errors are surfaced as `std::system_error` using the Windows
system category.

## WFP boundary

This is a user-mode transport primitive. It does not install policy, parse
HTTP, recover an original redirect destination, or prevent redirect
recursion. `<ntl/net/tls/stream>` is the separate Schannel TLS layer built above
it; WFP connect redirection and application framing remain separate concerns.
See [User-mode Schannel TLS streams](./tls-stream.md).

The [`stream-edit` controller](../../examples/wfp/stream-edit) is the runtime
proof. Its actual loopback client/server path uses `co_await read_exactly()` and
`co_await write_all()` while the kernel callout replaces a token split across
two writes. Its `--coroutine-self-test` mode also verifies fragmented
loopback transfer, `CancelIoEx`, and incomplete-read EOF behavior without
requiring WFP policy.
