# NTL WFP user TCP content filter

This example makes a bounded, fail-closed user-mode decision for complete
inbound TCP application messages. TCP is a byte stream, so it is intentionally
separate from the UDP example.

## Sample wire format

TCP first uses this example-only message framing:

```text
[u32 big-endian record length][one content-filter record]
```

The four-byte prefix is not a TCP header or standard. The shared record is:

```text
["NTLR"][version=1][classification][flags=0]
[u32 rule-id][u32 body length][body]
```

`ordinary` is permitted and `restricted` is blocked. The parser also validates
magic, version, flags, a nonzero rule ID, exact body length, and the 4 KiB body
limit. The allowed acceptance record deliberately contains the text `BLOCKME`
to prove that policy is based on typed fields rather than substring tricks.

## Enforcement path

1. IPv4/IPv6 ALE flow-established callouts associate typed flow state.
2. `STREAM_V4/V6` requests enough bytes for one complete bounded frame.
3. Inbound bytes are deferred and copied to driver-owned storage.
4. A reliable NTL RPC notification reaches the user coroutine.
5. `permit` resumes exactly that frame; `block` or `malformed` drops the flow.
6. Timeout, session loss, overload, allocation/publish failure, missed bytes,
   and unload cancel outstanding work fail closed.

The example executable is only the real policy service:

```text
crtsys_wfp_tcp_content_filter_policy_service.exe
  --port <fixture-port>
  --ready-file <path> --stop-file <path> --stats-file <path>
  --expected-requests <count> [--behavior normal|failure]
```

It installs the ephemeral IPv4/IPv6 policy, consumes reliable RPC inspection
requests, submits typed verdicts, publishes a ready signal, and writes driver
statistics before removing the policy on exit. It contains no listener,
traffic generator, exchange helper, or PASS judgment.

The separately built
`crtsys_wfp_tcp_content_filter_acceptance.exe` lives under
`test/wfp/runtime/fixtures/user/tcp-content-filter`. It starts the policy
service itself, waits for the ready file, generates split-prefix and same-flow
IPv4/IPv6 traffic, validates structured permit/block/malformed decisions and
policy removal, then creates the stop file. Its `--failure-self-test` mode
owns the timeout, invalid-verdict, late-verdict, and cancellation traffic.

The kernel counterpart uses the same framing and record parser but decides
synchronously in the callout. It therefore has no RPC verdict queue. See
`examples/wfp/kernel/tcp-content-filter`.
