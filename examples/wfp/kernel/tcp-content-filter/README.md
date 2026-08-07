# NTL WFP kernel TCP content filter

This example applies the same bounded wire contract as the user TCP example,
but parses and decides synchronously inside the WFP stream callout.

```text
[u32 big-endian record length]
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body length][body]
```

The prefix is an example application protocol, not part of TCP. `ordinary` is
permitted, `restricted` drops the flow, and malformed/oversized/incomplete or
missed stream data fails closed. The allowed body deliberately contains
`BLOCKME`; only the typed classification controls policy.

ALE flow-established callouts associate state for IPv4 and IPv6. No payload or
verdict crosses into user mode.

This intentional difference removes the user example's RPC queue, timeout,
coroutine, cancellation, and stream continuation. There is no asynchronous
work to drain on unload: the driver first stops acceptance, unregisters the
callouts (waiting for callbacks), releases flow targets, and removes the
device. Flow-association failure and unavailable callouts remain fail closed.

The elevated
`crtsys_wfp_kernel_tcp_content_filter_controller.exe` accepts `--port`,
`--ready-file`, `--stop-file`, and `--stats-file`. It only opens the driver,
installs port-scoped policy, publishes readiness, reports IOCTL counters, and
removes policy on exit. It contains no listener, traffic exchange, malformed
input generator, or PASS judgment.

The separately built
`crtsys_wfp_kernel_tcp_content_filter_acceptance.exe` lives under
`test/wfp/runtime/fixtures/kernel/tcp-content-filter`. It launches the
controller, waits for readiness, owns IPv4/IPv6 split framing and typed
permit/block/malformed traffic, requests controller shutdown, validates the
reported counters, and proves restoration after policy removal.
