# WFP stream-edit

[한국어 설명](./README.ko-KR.md)

This inline stream editor replaces the equal-length outbound TCP token
`BLOCKME` with `REDACT!`.

- `ALE_FLOW_ESTABLISHED_V4` attaches typed editor state.
- `STREAM_V4` scans the NBL/MDL scatter view directly, including a token split
  across fragment boundaries, without a fixed stack flattening buffer.
- A partial token suffix is retained by enforcing only the safe prefix, or by
  returning `NEED_MORE_DATA` when the whole indication is a token prefix.
- Replacement bytes are allocated as an owned NBL/MDL/buffer bundle and
  injected with the flow's typed injection site.
- Only after injection succeeds are the source token bytes blocked.
- Allocation, copying, or injection failure drops the connection.

The injector waits for all asynchronous completions before freeing its NBL
pool, MDLs, backing buffers, or injection handle.

The controller's real client/server data path is also asynchronous. It creates
overlapped sockets associated with `io_completion_context`, starts a
coroutine `read_exactly()` on the accepted socket, and sends each token
fragment through `co_await write_all()`. No blocking receiver thread is used.
Run the network-only cancellation and EOF contract without policy using:

```powershell
crtsys_wfp_stream_edit_app.exe --coroutine-self-test
```

The supported scope is one equal-length token on outbound IPv4 TCP. A full OOB
queue, busy-threshold backpressure, variable-length rewriting, IPv6, and
protocol-aware text semantics require additional state machines and tests.
