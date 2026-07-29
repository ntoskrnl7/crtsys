# Advanced WFP VM acceptance

This gate packages and test-signs the nine advanced WFP samples, enables
Driver Verifier for the selected drivers in one boot, runs every selected
controller repeatedly, checks load/unload counters plus crash events and
dumps, restores the caller-supplied Verifier configuration, and repeats the
crash/dump check for the restored boot.

The scripts are environment-neutral. VM paths, accounts, credentials, guest
staging paths, artifact paths, SDK/toolset versions, iteration counts, and the
Verifier state to restore are parameters. No checkout path or VM identity is
embedded in the fixture.

`Run-WfpAdvancedVmAcceptance.ps1` performs exactly two guest operating-system
restarts during a successful run:

1. apply the temporary selected-driver Verifier target list; and
2. restore and verify the supplied pre-existing target list.

It does not revert a snapshot or reset VM state.

The deterministic runtime proofs are:

- `datagram-proxy`: the proxy UDP socket exclusively receives the redirected
  datagram, then the original socket receives after dynamic policy removal;
- `async-inspection`: permit and block decisions both traverse delayed ALE
  reauthorization, and the blocked connection recovers after removal;
- `flow-monitor`: an observation-only callout reports selected TCP flow and
  stream byte telemetry through a read-only IOCTL;
- `stream-edit`: `BLOCKME`, split across send calls, arrives as equal-length
  `REDACT!`, then passes unchanged after policy removal; its controller also
  runs the IOCP coroutine read/write/cancel/EOF and bounded dynamic-message
  framing self-tests. The policy
  iterations use the same `co_await read_exactly()` and `write_all()` payload
  path rather than a separate blocking socket implementation.
- `connect-redirect`: a selected TCP connection reaches the local proxy,
  the proxy obtains the original endpoint plus opaque redirect records,
  connects the outbound leg without a redirect loop, relays request and
  response with two IOCP coroutines for both IPv4 and IPv6, and policy
  removal restores direct connections for both families.
- `bind-redirect`: typed IPv4 and IPv6 ALE bind requests are rewritten to
  selected loopback ports, then return to ephemeral binding after dynamic
  policy removal.
- `tls-inspection-proxy`: the same redirect handoff feeds a user-mode
  Schannel server leg and a separately validated Schannel client leg. A
  fragmented ClientHello selects and caches a per-SNI CA-signed leaf,
  bounded HTTP/1.1 framing exposes the plaintext body, `ALLOW` reaches the
  origin, `BLOCKME` is rejected before its HTTP request reaches the origin,
  the trust store remains unchanged, and policy removal restores a direct
  TLS connection.
- `udp-content-filter`: WFP handles two complete outbound UDP datagrams. A
  user-mode coroutine returns typed content verdicts, the allowed clone is
  reinjected, the blocked datagram is discarded, and policy removal restores
  normal delivery. Its one-time self-test proves that late, malformed,
  timed-out, and over-quota verdict paths are fail-closed.
- `tcp-content-filter`: WFP assembles two complete messages from an explicitly
  selected inbound TCP sample protocol. The allowed frame resumes, a message
  containing `BLOCKME` drops its flow, and policy removal restores normal
  delivery. Its one-time self-test proves malformed-verdict rejection,
  timeout flow drop, and late-permit rejection.

`Run-WfpAdvancedSuite.ps1 -SelectedSample stream-edit` may also be run
directly in an already prepared test guest. That same-boot check loads and
unloads only stream-edit and does not change Verifier settings or request a
restart.

Starting `datagram-proxy` also runs a real NBL whose UDP header crosses two
MDLs. Starting `flow-monitor` runs the bounded coroutine-reader fragmentation,
timeout, cancellation, EOF, competing-reader, and capacity-limit contracts.
Because service start fails if either contract fails, both execute while the
drivers are targeted by Verifier.

The runner also accepts:

- `-SelectedWfpSample` to limit the WFP drivers and controllers in one run;
- `-ManagedHttp3Url` to add the browser HTTP/3 WFP driver and managed-client
  redirect test.

The managed HTTP/3 redirect intentionally does not allocate an
original-destination context: its managed client already owns SNI and
`:authority`, and the proxy never calls
`SIO_QUERY_WFP_CONNECTION_REDIRECT_CONTEXT`. Ordinary TCP browser and
connect-redirect paths keep that context because their proxies require the
original endpoint.
