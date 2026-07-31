# Advanced WFP VM acceptance

This gate builds, packages, and test-signs only the selected advanced WFP
samples, enables the normal Driver Verifier configuration for those drivers
in one boot, runs every selected controller repeatedly, runs Systematic
Low Resources Simulation in a separate Verifier boot, checks load/unload
counters plus crash events and dumps, restores the caller-supplied Verifier
configuration, and repeats the crash/dump check for the restored boot.

The scripts are environment-neutral. VM paths, accounts, credentials, guest
staging paths, artifact paths, SDK/toolset versions, iteration counts, and the
Verifier state to restore are parameters. No checkout path or VM identity is
embedded in the fixture.

`Run-WfpAdvancedVmAcceptance.ps1` uses three guest operating-system restarts
during a successful full run:

1. apply the temporary selected-driver Verifier target list; and
2. replace it with a separate selected-driver Systematic Low Resources
   Simulation configuration; and
3. restore and verify the supplied pre-existing target list.

`-SkipLowResourcePass` omits step 2, leaving two restarts. Low Resources is
not enabled with volatile settings: volatile settings replace rather than add
to registry settings, and boot-time/DDI rule classes can reject that
transition. The separate boot also verifies driver load-time allocation
paths.

The dedicated Low Resources boot defaults to `-LowResourceMode Systematic`.
On Windows 11 this uses rule classes 19 and 36, resets its stack history,
enables runtime injection independently for each selected sample, advances
the Systematic test-pass counter after every bounded injection pass, and
requires both a nonzero `InjectionCount` and a successful recovery run after
runtime injection is disabled for every sample.
`-SystematicInjectionPassesPerSample` defaults to four; it deliberately does
not require exhausting an open-ended set of newly discovered call stacks.
Normal behavior is already proved in the preceding baseline boot.
Randomized mode remains
available for exploratory stress; its default probability is `10000/10000`
and it requires a nonzero deliberate-failure counter.

The default `-RestartMode Manual` never sends a restart command. At every
restart boundary it waits up to `-ManualRestartTimeoutSeconds` for the
operator to restart the VM. Use this mode for test-signed drivers when the
operator must select **Disable driver signature enforcement** in Windows
startup settings. Fully signed or test-signing-enabled automation may opt in
to `-RestartMode Automatic`.

Manual mode schedules temporary Verifier settings as `Persistent`, because
entering Windows Startup Settings can consume a `Oneboot` reservation before
the final operator-selected Windows boot. The runner still restores the
caller-supplied configuration after the test. Automatic mode uses `Oneboot`.

The gate does not revert a snapshot or reset VM state.

A normal-Verifier baseline and the Low Resources boot are separate evidence.
Do not report Low Resources as passed unless
`verifier-low-resource-active.txt`, every selected low-resource attempt log,
the per-sample Systematic statistics and post-disable recovery proof (or Randomized
nonzero deliberate-failure counter), cleanup evidence, and the post-restore
crash/dump checks all exist for the same run. Preparing the Low Resources
settings without completing the operator-controlled restart is not a pass.

The deterministic runtime proofs are:

- `datagram-proxy`: IPv4 and IPv6 proxy UDP sockets exclusively receive their
  redirected datagrams, then both original sockets receive after
  session-scoped policy removal;
- `async-inspection`: IPv4 and IPv6 permit and block decisions traverse
  delayed ALE reauthorization, and both blocked connections recover after
  removal;
- `flow-monitor`: observation-only IPv4 and IPv6 callouts report selected TCP
  flow and stream byte telemetry through a read-only IOCTL;
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
- `udp-content-filter`: WFP handles complete outbound IPv4 and IPv6 UDP
  datagrams. One user-mode coroutine policy permits/reinjects or blocks them,
  and removal restores both families. Its one-time self-test proves that
  late, malformed, timed-out, and over-quota verdict paths are fail-closed.
- `tcp-content-filter`: WFP assembles complete messages from explicitly
  selected inbound IPv4 and IPv6 TCP sample-protocol flows. The shared
  user-mode policy resumes allowed frames, drops blocked flows, and removal
  restores both families. Its one-time self-test proves malformed-verdict
  rejection, timeout flow drop, and late-permit rejection.

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

- `-SelectedWfpSample` to limit the drivers, builds, packaged artifacts, and
  controllers in one run;
- `-ManagedHttp3Url` to add the browser HTTP/3 WFP driver and managed-client
  redirect test.

The managed HTTP/3 redirect intentionally does not allocate an
original-destination context: its managed client already owns SNI and
`:authority`, and the proxy never calls
`SIO_QUERY_WFP_CONNECTION_REDIRECT_CONTEXT`. Ordinary TCP browser and
connect-redirect paths keep that context because their proxies require the
original endpoint.

## Same-boot soak and third-party coexistence

`Run-WfpAdvancedSoak.ps1` repeatedly invokes the already validated load,
unload-race, fail-closed, dual-stack, redirect, TLS, and 20,000-flow contracts
for a requested wall-clock duration. It does not change Verifier state and does
not restart the machine. Each cycle records elapsed time, the flow monitor's
reported IPv4/IPv6 p95 latency, available/committed memory, paged and nonpaged
pool, process/thread counts, WFP state before and after, crash events, and new
dump files in a machine-local evidence directory.

Use `-RequiredProviderPattern` for a VPN, firewall, or WebFilter test machine.
Every supplied regular-expression pattern must exist in both WFP snapshots, so
the suite proves that NTL ran while the named provider remained registered. A
real coexistence claim still belongs to the machine that has that product and
representative traffic installed; the script never fabricates that evidence.

```powershell
.\Run-WfpAdvancedSoak.ps1 `
  -PackageRoot C:\wfp-advanced `
  -DurationMinutes 480 `
  -IterationsPerCycle 3 `
  -RequiredProviderPattern 'Contoso VPN','Enterprise Web Filter' `
  -EvidenceDirectory C:\wfp-evidence\soak-8h
```
