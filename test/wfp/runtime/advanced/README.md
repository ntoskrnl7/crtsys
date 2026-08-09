# Advanced WFP VM acceptance

This gate builds, packages, and test-signs only the selected advanced WFP
samples, stages them in an already running disposable Windows guest, runs
every selected controller repeatedly, and checks Driver Verifier load/unload
counters, crash events, dumps, and the unchanged Verifier configuration.

The scripts are environment-neutral. VM paths, accounts, credentials, guest
staging paths, artifact paths, SDK/toolset versions, and iteration counts are
parameters. No checkout path or VM identity is embedded in the fixture.

The VM runner deliberately has no reboot, snapshot, Driver Verifier reset, or
Driver Verifier configuration operation. Before invoking it, the operator
must:

1. configure every selected driver as a Verifier target;
2. boot the guest manually, including choosing **Disable driver signature
   enforcement** when the test-signed package requires it; and
3. mark the machine as a disposable test guest by creating
   `C:\crtsys-disposable-test-guest.sentinel` with the exact contents
   `CRTSYS_DISPOSABLE_TEST_GUEST`.

The runner reads `verifier /query` and `verifier /querysettings` before and
after the suite. It refuses to run when a selected driver is not already a
Verifier target, requires its load and unload counters to increase during this
run, writes `verifier-load-unload-evidence.json`, and requires the settings
text to remain identical. The user owns every boot and every Verifier change.

`-RuntimeOnly` is the explicit exception for a same-boot functional run whose
selected driver is not a current Verifier target. It retains the disposable
guest guard, crash-event and dump baselines, and byte-for-byte Verifier
settings comparison, but neither requires nor claims a selected-driver
Verifier load observation. The default remains the strict Verifier gate.

Low Resources Simulation is an explicit, opt-in gate. The operator prepares
the boot and Verifier targets; the runner never changes Verifier settings or
reboots the guest. The separate Random Low Resources acceptance run for
`kernel-browser-https-inspection` observed one intentional allocation failure,
proved fail-closed cleanup with no remaining driver or process, found no new
crash event or dump, and preserved the exact Verifier settings. This result is
separate from, and is not implied by, the normal Verifier gate.

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
  `REDACT!`, then passes unchanged after policy removal; its acceptance fixture
  also runs the IOCP coroutine read/write/cancel/EOF and bounded dynamic-message
  framing contracts. The policy
  iterations use the same `co_await read_exactly_borrowed()` and `write_all()` payload
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
- `kernel-connect-redirect`: the driver captures the accepted WSK socket's
  original tuple and opaque WFP redirect records, applies the records before
  the outbound connect, and performs the IPv4/IPv6 bidirectional relay without
  a user-mode data plane.
- `kernel-tls-inspection-proxy`: driver-owned WSK preserves the original tuple
  and redirect records, kernel Schannel terminates both TLS legs with system
  validation on the origin leg, and the common transform pipeline inspects and
  rewrites bounded HTTP/1.1 and HTTP/2 traffic. Acceptance proves allowed
  traffic reached IPv4/IPv6 TLS origins, blocked traffic did not, malformed and
  idle ClientHello paths failed closed, and cursor evidence was retained.
- `kernel-browser-https-inspection`: the driver owns its WSK, Schannel, and
  MsQuic paths while a separate controller owns policy and driver commands.
  The acceptance fixture issues controlled identities and proves HTTP/1.1,
  HTTP/2, and HTTP/3 permit/block plus captured request/HTML content through a
  bounded control protocol. It never changes browser settings or launches a
  browser.
- `kernel-http3-inspection`: the driver binds to the existing inbox MsQuic NMR
  provider, terminates TLS 1.3/QUIC, exchanges HTTP/3 SETTINGS, decodes bounded
  QPACK, and returns content-selected 200/403 responses over IPv4 and IPv6.
- `kernel-udp-content-filter`: complete IPv4/IPv6 datagrams are inspected and
  permitted or blocked directly by the driver.
- `kernel-tcp-content-filter`: split IPv4/IPv6 byte streams are framed and
  permitted or blocked directly by the driver with bounded buffering.

`Run-WfpAdvancedSuite.ps1 -SelectedSample stream-edit` may also be run
directly in an already prepared disposable test guest. The explicit switch
and sentinel are both required because this command installs and removes a
test driver service:

```powershell
.\Run-WfpAdvancedSuite.ps1 `
  -PackageRoot C:\wfp-advanced `
  -SelectedSample stream-edit `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

That same-boot check loads and unloads only stream-edit and does not change
Verifier settings or request a restart.

`specialized-observation` defaults to deterministic endpoint-closure signals.
To require real Ethernet classify traffic, pass a reachable IPv4 address that
answers ICMP echo and add `-SpecializedObservationRequireMac`. A Hyper-V
topology can additionally use `-SpecializedObservationRequireVSwitch`:

```powershell
.\Run-WfpAdvancedVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestPassword $guestPassword `
  -SelectedWfpSample specialized-observation `
  -SpecializedObservationTrafficTarget '<reachable-ipv4>' `
  -SpecializedObservationRequireMac `
  -SpecializedObservationTrafficDurationMs 5000
```

The runner does not create a vSwitch or alter network topology. The operator
must provide matching traffic and selects the required evidence explicitly.

## Hyper-V vSwitch and IPsec evidence

On 2026-08-10, the environment-specific `specialized-observation` paths were
exercised in a disposable nested Hyper-V lab. A Windows L1 guest hosted an
internal switch with `192.168.250.1/24` on its management-OS adapter, and a
Windows L2 traffic peer used `192.168.250.2/24` on that switch. The driver,
Driver Verifier, and temporary policy ran only in L1; L2 was only a traffic
peer and did not require a test-signing boot or Driver Verifier.

The vSwitch gate requires the inbox **Microsoft Windows Filtering Platform**
switch extension to be enabled and running on the selected switch. With that
precondition, three iterations passed with `registered-mask=63`,
`exercised-mask=63`, and `required-mask=51`, proving endpoint IPv4/IPv6 and
both vSwitch directions. A diagnostic run with the extension disabled reached
only `exercised-mask=15`, so a registration-only result or ordinary ping is not
sufficient vSwitch evidence. The evidence set must retain the suite log, the
switch-extension state, and the before/after Verifier state. In the recorded
run the selected driver's Verifier counters changed from load/unload `2/2` to
`3/3`, while the Verifier settings remained byte-for-byte unchanged.

The IPsec gate used temporary machine pre-shared-key transport rules scoped to
the two peer addresses and separately selected TCP and UDP. Real TCP and UDP
nonce traffic established four Quick Mode SAs on each peer: inbound and
outbound rows for each protocol. The subsequent three-iteration driver run
passed with `registered-mask=63`, `exercised-mask=63`, and `required-mask=3`,
while Driver Verifier recorded a `+1` load and `+1` unload delta. The lower
required mask is intentional, and the Verifier settings remained unchanged:
IPsec policy layers are management-only, so the IPsec proof is the protected
TCP/UDP traffic plus the live Quick Mode SAs; it is not a claim that an IPsec
policy layer was registered as a classify callout.

A reusable evidence set for that gate includes the policy inventory,
before/after Quick Mode SA JSON from both peers, TCP/UDP listener results,
`specialized-observation-ipsec.log`,
`verifier-load-unload-evidence.json`, and scoped cleanup results. Cleanup must
remove only the test rule group, authentication set, listeners, and temporary
firewall rules. It must not stop or remove the built-in `IKEEXT` or
`PolicyAgent` services. The recorded cleanup confirmed both peers were free of
the test-scoped objects, left both built-in services running, and found no
remaining temporary driver service.

These environment-specific gates remain opt-in and read-only:

- `-RequireActiveIpsecSecurityAssociation` requires
  `specialized-observation` and a
  `-SpecializedObservationTrafficTarget` equal to the protected peer. The
  runner requires a matching Quick Mode SA before driver load, sends bounded
  ICMP traffic to that peer while the specialized policy is active, and
  requires the matching SA again afterward. It writes
  `ipsec-quick-mode-sa-before.json` and
  `ipsec-quick-mode-sa-after.json`. The operator must configure both peers and
  establish the SA; the runner does not create or remove connection-security
  policy.
- `-RequireLowResourcesSimulation` requires the current boot to have Random or
  Systematic Low Resources Simulation enabled before any selected driver is
  loaded. It also requires Driver Verifier's intentional-allocation-failure
  counter to increase and writes `low-resources-evidence.json`. A suite error
  after the measured injection is accepted only as a graceful fail-closed
  result when no staged driver service or process survives, Verifier settings
  remain byte-for-byte unchanged, and the crash/dump postcheck is clean. The
  runner still refuses to change Verifier or reboot the guest.

These switches prevent an ordinary registration-only run from being reported
as IPsec or allocation-failure runtime evidence.

Starting `datagram-proxy` also runs a real NBL whose UDP header crosses two
MDLs, so service start fails if that contract fails while the driver is
targeted by Verifier. `flow-monitor` starts only its real WFP observation and
telemetry path; the separate `test/net/kernel-contracts` driver owns the
bounded coroutine-reader fragmentation, timeout, cancellation, EOF,
competing-reader, capacity-limit, and unload-drain contracts.

The runner also accepts:

- `-SelectedWfpSample` to limit the drivers, builds, packaged artifacts, and
  controllers in one run;
- `-Configuration` and `-BuildRoot` to reuse an explicitly selected Debug or
  Release build tree with `-SkipBuild`, without falling back to an implicit
  artifact location;
- `-RuntimeOnly` to run selected drivers without claiming that those drivers
  were Verifier targets, while retaining crash/dump checks and the read-only
  Verifier-settings comparison;
- `-MsQuicDllPath` to supply a compatible official `msquic.dll` for the
  kernel HTTP/3 controller. If omitted, artifact preparation resolves the
  pinned `Microsoft.Native.Quic.MsQuic.Schannel` package version with NuGet;
  and
- `-ManagedHttp3Url` to add the browser HTTP/3 WFP driver and managed-client
  redirect test.

`Run-WfpAcceptanceMatrix.ps1` applies the same read-only boot/Verifier
contract to every JSON row. Each referenced VM must already be running with
the row's selected drivers configured as Verifier targets. The matrix schema
therefore has no restart, restore, or Low Resources options; it carries only
environment constraints, sample selection, and the disposable-guest sentinel
path.

The kernel HTTP/3 gate only stages the user-mode DLL used by its controller.
It never installs or replaces `msquic.sys`; the guest must already provide the
inbox kernel MsQuic provider, and preflight fails explicitly when it is absent.

The managed HTTP/3 tuple-translation path intentionally does not allocate an
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
  -EvidenceDirectory C:\wfp-evidence\soak-8h `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```
