# NTL WFP kernel UDP content filter

This example parses the same structured record as the user UDP example and
decides synchronously in `DATAGRAM_DATA_V4/V6`:

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body length][body]
```

`ordinary` is permitted, `restricted` is absorbed, and malformed headers,
inconsistent UDP lengths, unsupported packet topology, and oversized records
fail closed. The ordinary test body contains `BLOCKME`, proving the old
substring-marker shortcut is not used.

Because the verdict is final before classify returns, this kernel example has
no pending queue, RPC, clone, or reinjection. That is an intentional runtime
difference from `examples/wfp/user/udp-content-filter`, not missing coverage.
Unload only needs to stop new acceptance and unregister the synchronous
callouts; unavailable callouts are configured to block.

The elevated
`crtsys_wfp_kernel_udp_content_filter_controller.exe` accepts `--port`,
`--ready-file`, `--stop-file`, and `--stats-file`. It only opens the driver,
installs port-scoped policy, publishes readiness, reports IOCTL counters, and
removes policy on exit. It contains no sender, receiver, malformed datagram, or
PASS judgment.

The separately built
`crtsys_wfp_kernel_udp_content_filter_acceptance.exe` lives under
`test/wfp/runtime/fixtures/kernel/udp-content-filter`. It launches the
controller, waits for readiness, owns IPv4/IPv6 typed
permit/block/malformed traffic, requests shutdown, validates counters, and
proves restoration for both families after policy removal. Run acceptance only
with the driver installed in a test-signing VM.
