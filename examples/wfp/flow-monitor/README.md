# WFP flow-monitor

[한국어 설명](./README.ko-KR.md)

This observation-only driver/controller pair covers the core WDK `msnmntr`
flow-lifetime mechanism without its protocol-specific application parsing.

- `ALE_FLOW_ESTABLISHED_V4` creates typed per-flow state.
- `STREAM_V4` counts indications, indicated bytes, and missed bytes.
- Flow deletion destroys the typed context and increments the closed count.
- A secure, administrator/system-only control device exposes a read-only
  typed stats IOCTL.

Both filters use the inspection action. The stream adapter forces
`FWP_ACTION_CONTINUE`, so this sample cannot accidentally turn telemetry into
traffic enforcement.

Driver load also runs the bounded coroutine-reader contract used by
observation code: a header and body arrive in separate fragments, sequential
reads resume at `PASSIVE_LEVEL`, and timeout, cancellation, EOF, competing
reader, and buffer-limit paths complete exactly once. This self-test copies
its input; it never retains callback-scoped WFP storage.

The supported surface is flow association, byte-count observation, deletion,
and user-mode telemetry transfer. Application parsing, payload retention, and
persistent audit storage belong in a policy-specific consumer.
