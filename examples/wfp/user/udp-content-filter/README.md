# NTL WFP user UDP content filter

This example makes bounded, fail-closed user-mode decisions for complete
outbound IPv4 and IPv6 UDP datagrams. UDP already preserves message boundaries,
so one datagram payload contains exactly one shared content-filter record and
does not need the TCP length-prefix framer.

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body length][body]
```

The structured parser permits `ordinary`, blocks `restricted`, and rejects
invalid headers or lengths. An ordinary test body contains `BLOCKME` to prove
that classification is not a substring search.

## Enforcement path

1. `DATAGRAM_DATA_V4/V6` validates the complete UDP header and bounded payload.
2. The driver copies bounded payload and builds a move-only transport-send
   request that owns endpoint, compartment, remote address, IPv6 scope, and
   transport control data through asynchronous injection completion.
3. It clones and absorbs the original datagram.
4. A reliable NTL RPC notification reaches the user coroutine, which parses
   the structured record.
5. `permit` reinjects the clone; `block`/`malformed` releases it without send.
6. The pending limit is reserved before passive work is queued. Timeout,
   session loss, overload, allocation/publish/injection failure, and unload
   remain fail closed. Self-injected packets are recognized and pass; injector
   teardown drains completion-owned clones and metadata.

`crtsys_wfp_udp_content_filter_policy_service.exe` contains only the real
policy path. It accepts `--port`, `--ready-file`, `--stop-file`, `--stats-file`,
`--expected-requests`, and `--behavior normal|failure`; installs the ephemeral
policy; consumes reliable RPC requests; returns typed verdicts; and writes
driver statistics. It contains no sender, receiver, malformed traffic, or PASS
judgment.

`crtsys_wfp_udp_content_filter_acceptance.exe`, built from
`test/wfp/runtime/fixtures/user/udp-content-filter`, launches the service,
waits for its ready file, owns all IPv4/IPv6 traffic and structured permit/block/malformed
assertions, creates the stop file, and verifies policy removal. Its
`--failure-self-test` mode owns pending-bound, timeout, invalid and
late verdict, and session-cancellation traffic.

The kernel counterpart uses the same record parser but decides synchronously,
so it intentionally needs neither RPC nor clone/reinjection.
