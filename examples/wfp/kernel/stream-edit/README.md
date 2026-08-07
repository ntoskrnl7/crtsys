# WFP kernel stream-edit

[한국어 설명](./README.ko-KR.md)

This sample edits a selected outbound IPv4 TCP stream. The inline path replaces
equal-length `BLOCKME` with `REDACT!` across indication boundaries. The bounded
out-of-band path clones pass-through data and can inject a variable-length
replacement for `OOBBLOCK`.

The example is intentionally limited to real policy/data-path code:

- `crtsys_wfp_stream_edit` is the driver and owns stream buffering, injection,
  completion, and bounded OOB work.
- `crtsys_wfp_stream_edit_controller` installs the port-scoped ephemeral
  policy. It does not create sockets or run coroutine tests.
- `crtsys_wfp_stream_edit_acceptance` is built from
  `test/wfp/runtime/fixtures/kernel/stream-edit`. It owns client/server traffic,
  split-boundary and OOB assertions, cleanup checks, and coroutine contracts.

Controller contract:

```text
--port <1..65535>
--ready-file <path> --stop-file <path> --stats-file <path>
[--duration-ms <100..300000>]
```

Default acceptance launches the sibling controller, waits for ready, sends
traffic, signals stop, reads stats, and verifies that original bytes return
after policy removal. Use `--controller <path>` to select another controller.

The socket/coroutine contract is host-only and does not require an installed
driver:

```powershell
crtsys_wfp_stream_edit_acceptance.exe --coroutine-contract
ctest --test-dir artifacts\examples\wfp-stream-edit -C Debug
```

It covers IOCP read/write, cancellation, EOF, split prefixes, coalesced framed
messages, and the exact bounded OOB pending-budget implementation used by the
driver. These checks live under `test`; no synthetic self-test runs while the
product driver is loading.

```powershell
cmake -S examples\wfp\kernel\stream-edit `
      -B artifacts\examples\wfp-stream-edit -A x64
cmake --build artifacts\examples\wfp-stream-edit --config Debug
```
