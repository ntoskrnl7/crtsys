# WDK WFP sample coverage

This document maps the reusable mechanisms in
`Windows-driver-samples/network/trans` to `ntl::wfp`. Coverage means that the
mechanism has a typed API and compile contract. Runtime status is stated
separately; a compile-covered advanced path is not reported as runtime-tested.

## Coverage matrix

| Microsoft sample | Reusable mechanisms | NTL surface | Evidence/status |
| --- | --- | --- | --- |
| `ddproxy` | ALE flow discovery, typed per-flow proxy state, datagram classify, NBL clone, block/absorb, asynchronous reinjection | `ale_flow_established_v4/v6`, `datagram_data_v4/v6`, `flow_target`, `add_flow_context`, `cloned_packet`, `transport_injector` | `/W4 /WX`; IPv4 and IPv6 end-to-end UDP redirect each passed 20 iterations under Driver Verifier; a load-time NBL fixture also edits a UDP field split across MDLs |
| `inspect` | ALE pending operation, transport packet retention, asynchronous worker decision, clone/reinject, unload drain | `pended_operation`, `referenced_packet`, `cloned_packet`, network/transport injectors with rundown | `/W4 /WX`; IPv4 and IPv6 delayed permit/block plus policy-removal recovery each passed 20 iterations under Driver Verifier |
| `msnmntr` | flow-established observation, stream flow context, monitor-only stream classification, flow-delete cleanup | flow-established and stream tags, `inspection_filter_builder`, `add_stream`, `flow_target`, `stream_event` | `/W4 /WX`; IPv4 and IPv6 flow/stream telemetry passed 20 iterations under Driver Verifier; the separate kernel-contract driver covers bounded coroutine-reader fragmentation, timeout, cancel, EOF, competing-reader, and limit paths |
| `stmedit` inline | stream byte copy, permit/block byte ranges, need-more-data, drop connection | `stream_data_view`, `scatter_view`, `scan_bytes`, `stream_result`, `stream_filter_builder`; user-mode `io_completion_context` and `async_socket` | `/W4 /WX`; split-send replacement and policy-removal restoration passed 20 iterations under Driver Verifier; the controller's real payload path uses IOCP coroutine exact-read/write-all operations, with cancellation and incomplete-EOF self-tests |
| User-mode content verdict | complete datagram retention; application-selected TCP framing; inbound defer/continue; bounded kernel-to-user delivery; typed permit/block result; timeout and queue backpressure | `content_view`, `udp_datagram_view`, `tcp_message_view`, `framing`, NTL RPC reliable notifications, `cloned_packet`, `transport_injector`, `stream_continuation` | `/W4 /WX`; IPv4 and IPv6 UDP and sample-protocol TCP permit/block plus restoration passed 20 iterations under Driver Verifier; one layer-erased continuation queue safely resumes either stream family; TCP block drops the whole flow; malformed, late, timed-out, and over-quota results are fail-closed |
| `stmedit` out-of-band | defer/continue, clone stream data, replacement injection, distinct stream-clone cleanup | `stream_result::defer`, `stream_injection_site`, `cloned_stream_data`, `stream_injector` | `/W4 /WX`; deferred clone/continue, split-boundary variable-length replacement, and bounded busy handling passed three iterations under Driver Verifier |
| `WFPSampler` basic action scenarios | layer registration, provider/sublayer/callout/filter graph, permit/block/continue/absorb | typed keys/layers, `callout_driver`, `terminating_decision`, `arbitration_decision`, void inspection callbacks, transactional management, `policy_session`, `policy_manifest` | Representative `ALE_AUTH_CONNECT_V4` path runtime- and Driver-Verifier-tested; persistent reconcile, controller-close survival, health, and explicit uninstall are runtime-tested |
| `WFPSampler` packet/flow/stream scenarios | transport/datagram layers, flow association, packet and stream injection | packet/flow/stream APIs above | Reusable mechanisms compile-covered; the sample's large scenario CLI is not reproduced |
| `WFPSampler` connect redirect | writable `FWPS_CONNECT_REQUEST`, local proxy PID/port, redirect loop tracking, original-destination context, accepted-socket redirect-record propagation, two-leg relay | `ale_connect_redirect_v4/v6`, `connect_redirector`, `local_proxy_target`, `connect_redirect_filter_builder`, `redirected_connection`; user-mode `async_socket` relay; kernel `wsk_redirected_connection` and `async_transport_stream` relay | `/W4 /WX`; the user path's IPv4/IPv6 proxying, loop-free reconnect, coroutine relay, and policy-removal restoration passed three iterations under Driver Verifier; the paired kernel fixture's IPv4/IPv6 original-destination relay, redirect records, directional byte counters, and clean unload passed three same-boot VM iterations with no new crash/dump and unchanged Verifier settings; that runtime-only run is not claimed as a Verifier-target result |
| `WFPSampler` bind redirect | writable `FWPS_BIND_REQUEST`, dual-stack address/port rewrite, reservation token, previous-version loop guard | `ale_bind_redirect_v4/v6`, `bind_redirect_selector`, `local_bind_target_v4/v6`, `bind_redirector`, `bind_redirect_filter_builder` | `/W4 /WX`; IPv4 and IPv6 selected-port binding plus post-policy ephemeral restoration passed three iterations under Driver Verifier |
| User-mode TLS inspection proxy | connect redirect, accepted-leg TLS termination, original-destination TLS client leg, plaintext application framing and typed policy | WFP connect-redirect surface above; user-mode `tls_credentials`, `tls_stream`, `framing`, and `inspection` | `/W4 /WX`; Schannel client/server runtime contracts pass on x64/x86; redirected permit/block and direct-policy-removal restoration are in the advanced VM gate |
| Browser HTTPS inspection | application-scoped TCP redirect, native UDP/443 fallback enforcement, dynamic per-SNI identity, persistent HTTP/1.1, multiplexed HTTP/2, WebSocket, bounded content codecs, and managed HTTP/3/WebTransport inspection | WFP and user-mode TLS/HTTP/QUIC surfaces above, in an independent driver/app contract | `/W4 /WX`; deterministic HTTP/1.1, HTTP/2, and managed HTTP/3 contracts are packaged separately from the Internet-dependent isolated-browser proof in `test/wfp/runtime/https-live`; a stock browser's private-CA HTTP/3 trust boundary is not reported as transparent H3 inspection |
| Direct kernel application-content policy | WSK TCP/UDP, kernel Schannel, CNG/DER X.509, kernel zlib/Brotli, direct HTTP/1.1 and HTTP/2 framing/transform policy, and kernel MsQuic HTTP/3 | `ntl::net::kernel` transports/providers with the shared `ntl::net` protocol core | `/W4 /WX`; the kernel browser aggregate passed three same-boot iterations as a preconfigured Driver Verifier target: HTTP/1.1, HTTP/2, and HTTP/3 inspect/block/rewrite, compression, gRPC, WebSocket/Extended CONNECT/WebTransport, IPv4/IPv6 WFP paths, workspace lifetime/exhaustion, connection churn, and clean drain; the gate found no new crash/dump and preserved the exact Verifier settings |
| Direct kernel HTTP/3 | MsQuic NMR provider, TLS 1.3/QUIC lifecycle, SETTINGS, bounded dynamic/Huffman QPACK with blocked-stream resume and acknowledgement, gzip/deflate/Brotli, Extended CONNECT, and WebTransport streams/Datagrams/capsules/reliable reset | `kernel::msquic_provider`, shared QUIC/HTTP/3/QPACK/WebTransport contracts | `/W4 /WX`; x64 Debug and Release build with an official controller DLL; the browser aggregate's kernel HTTP/3 path passed three iterations under Driver Verifier, including origin H3 negotiation, peer SETTINGS, QPACK acknowledgement, multiplexing, quota reclamation, WebTransport policy, UDP relay, and clean drain; the standalone `kernel-http3-inspection` fixture also passed three isolated load/run/unload iterations, each with 96 sequential connections, IPv4/IPv6 policy removal/restoration, unavailable-callout fail-close, compression, WebTransport, capture, and no new crash/dump; that standalone run preserved Verifier settings but is not claimed as a Verifier-target result |
| `WFPSampler` specialized scenarios | IPsec policy, MAC/frame, vSwitch, name-resolution cache, endpoint closure, fast-layer metadata | typed specialized layer tags, capability categories, and compile contracts | `/W4 /WX`; endpoint-closure IPv4/IPv6 and inbound/outbound MAC classify passed three iterations under Driver Verifier with the explicit MAC requirement; Hyper-V vSwitch classify passed three iterations with masks `63/63/51` after enabling the inbox WFP switch extension; active transport-mode IPsec produced bidirectional TCP and UDP Quick Mode SAs on both peers, and the associated Verifier run recorded a `+1/+1` load/unload delta; IPsec layers remain management-only and fast layers remain introspection-only rather than legal static-callout targets |

## ALE connect-block runtime result

The x64 `ale-connect-block` driver and controller were exercised in a
disposable Windows 11 x64 VM:

1. driver registration and ephemeral policy installation succeeded;
2. the selected loopback TCP connection failed with `WSAEACCES`;
3. closing the ephemeral policy session removed policy and the connection
   succeeded;
4. a persistent manifest survived controller close, reported healthy from a
   new engine connection, continued enforcing, and was explicitly uninstalled;
5. twenty repeated ephemeral policy/install/remove iterations passed; and
6. the same-boot lifecycle run produced no crash event or dump and left the
   VM's pre-existing Verifier target list unchanged.

The repository acceptance scripts and logs are the repeatable evidence. This
result validates the common ALE lifecycle and unload path.

## Advanced runtime result

The x64 baseline advanced gate can select any subset without building or
packaging unrelated samples. One dual-stack policy gate ran
`datagram-proxy`, `async-inspection`, `flow-monitor`, `udp-content-filter`,
and `tcp-content-filter` together for 20 iterations each under Driver
Verifier. A later full gate ran all ten advanced samples for two iterations
each, including stream edit, both redirect forms, TLS inspection, and
specialized observation. Together they passed:

1. 20 controller iterations for each of the five policy samples plus a
   two-iteration full ten-sample regression;
2. one verified driver load and unload for every target;
3. the fragmented UDP NBL/MDL load-time contract;
4. IPv4 and IPv6 redirect, delayed decision, telemetry, UDP verdict, and TCP
   verdict paths plus policy-removal restoration for both families;
5. the UDP content-filter malformed, late, timeout, and pending-limit
   fail-closed self-test plus the TCP content-filter malformed, timeout,
   late-permit, and flow-drop self-test before their separate iterations;
6. zero crash/reboot-error events and zero new kernel dumps; and
7. an unchanged operator-prepared Driver Verifier configuration before and
   after the same-boot suite.

The earlier foundation source state built its 13 WFP compile/example projects
for x64 and ARM64 in Debug and Release. The eight x64 semantic CTest contracts
passed in both configurations. The newly added paired kernel examples are not
included in that historical count or runtime claim. The final same-boot VM
audit for the earlier gate found no WFP service,
controller process, test certificate, project WFP policy, crash event, or new
dump, and found no unintended WFP service or policy left behind.

Separate extension gates passed stream-edit's deferred clone/continue,
variable replacement, and IOCP coroutine read/write/cancel/EOF contracts;
connect-redirect's IPv4/IPv6 coroutine relay; IPv4/IPv6 bind redirect; and
the TLS inspection proxy's two Schannel legs, decrypted permit/block verdicts,
unchanged trust store, `close_notify`, and direct TLS restoration. A later
browser HTTP/3 Verifier run exposed one
WFP-owned original-destination context still alive at immediate driver
unload. The managed UDP tuple-translation path does not allocate that
connect-redirect context; FLOW_ESTABLISHED retains the original tuple and
DATAGRAM_DATA performs bounded bidirectional clone/rewrite/reinjection. The
same external-QUIC-idle failure then unloaded cleanly under Verifier with
zero new crash events and dumps. Ordinary TCP proxy paths continue to request
the context.

The current direct-kernel gate separately ran the aggregate browser driver
three times as an operator-preconfigured Verifier target. Every iteration
covered HTTP/1.1, HTTP/2, and local managed HTTP/3 policy and transform paths,
workspace ownership/exhaustion, connection churn, and clean drain. A second
three-iteration runtime gate covered the smaller kernel TLS proxy's dual-stack
redirect records, two Schannel legs, HTTP/1.1 and HTTP/2 permit/block/rewrite,
fail-closed malformed/timeout handling, bounded capture, and cleanup. Both
gates found no new crash event or dump and left the byte-for-byte Verifier
settings unchanged; only the aggregate browser driver was asserted as a
Verifier target in these two runs.

The standalone kernel HTTP/3 gate additionally passed three isolated
driver load/run/unload iterations. Each iteration exercised 96 sequential
connections, IPv4/IPv6 WFP gates, TLS 1.3, dynamic QPACK resume and
acknowledgement, gzip/deflate/Brotli, Extended CONNECT/WebTransport,
policy removal and restoration, unavailable-callout fail-close, bounded
capture, and clean unload. It produced no new crash event or dump and left
Verifier settings unchanged; it was a runtime-only result, not a claim that
the standalone driver was configured as a Verifier target.

The current VM fixture never starts, resets, reverts, or reboots a VM and
never changes Driver Verifier. The operator first configures every selected
driver as a Verifier target, manually boots the disposable guest with the
required startup policy, and creates the deliberate disposable-guest
sentinel. The runner then reads `verifier /query` and
`verifier /querysettings`, executes the same-boot suite, and requires both the
selected-driver load/unload counters to increase and byte-for-byte unchanged
settings. Per-run deltas are written to
`verifier-load-unload-evidence.json`, so prior cumulative counts cannot satisfy
the current gate.
Paths, credentials, VM identity, guest staging, SDK/toolset, and iteration
count remain parameters rather than repository machine assumptions.

The normal Verifier baseline above is historical evidence and does not claim
Low Resources Simulation. The VM runner has an explicit
`-RequireLowResourcesSimulation` gate that refuses to load a driver unless
Random or Systematic Low Resources Simulation is already active in the
operator-prepared boot; it never enables the option itself. A separate Random
Low Resources run of `kernel-browser-https-inspection` increased Verifier's
intentional-allocation-failure counter from 2 to 3 and exercised the bounded
workspace/TLS/HTTP allocation path. The injected failure was accepted as a
safe fail-closed result only after the runner proved that no staged driver or
process remained, no crash event or dump appeared, and the Verifier settings
were byte-for-byte unchanged. The normal Verifier configuration was restored
and a subsequent specialized-observation load/unload run passed with a +1/+1
delta. An arbitrary suite failure is never converted to a pass.

## Safety properties

The public surface intentionally makes these invalid combinations
unrepresentable:

1. A callout or filter key cannot be used at a different layer.
2. A normal classify callback cannot mutate `FWPS_CLASSIFY_OUT0`.
3. A stream callback cannot return a terminating or arbitration decision,
   and a terminating, arbitration, or inspection callback cannot return a
   `stream_result`. Inspection callbacks return `void` and therefore cannot
   accidentally classify traffic.
4. Stream action and byte-count fields are created only by result factories.
   The adapter also rejects outbound defer, terminal-buffer need-more,
   inspection edits, and drop-connection under a non-unknown native filter.
5. A submitted flow context is destroyed exactly once: immediately if
   association fails, or by the registration's typed flow-delete trampoline
   after association succeeds. Reset stops new associations and drains all
   successful ones before unregistering the callout.
6. Normal packet clones and stream-data clones have different owner types and
   therefore different mandatory release functions.
7. Injection handles cannot be destroyed until their asynchronous completion
   rundown reaches zero.
8. Policy action types, engine handles, native condition arrays, commit, and
   abort are not exposed to controller code. Separate terminating,
   inspection, and unknown-action builders select the legal native action.
9. Provider/sublayer/callout capabilities from different transactions or
   providers cannot be combined.
10. Content policy returns a typed verdict rather than a native WFP action.
    The kernel retains injection/defer ownership and applies a bounded
    fail-closed timeout and queue policy. A rejected TCP application message
    drops its flow instead of deleting arbitrary stream bytes.
11. Connect redirection is available only at typed ALE connect-redirect
    layers. Its builder requires a nonzero proxy PID and port, the kernel
    operation pairs every writable-data acquisition with exactly one apply,
    and user mode cannot parse or manufacture WFP's opaque redirect records.
12. Each management layer exposes only conditions valid for that layer.
    Duplicate fields, invalid prefix lengths, unsupported direction/flag
    values, and non-atomic ICMP type/code pairs fail before a transaction.
13. Ephemeral policy uses `install`; persistent policy can only use
    `reconcile` or `uninstall`. Commit verifies that the exact provider,
    sublayer, callout, and filter key set matches the declared manifest.
14. Network-event telemetry copies native data into bounded, pointer-free
    snapshots and reports queue drops. Changing machine-wide collection state
    requires an explicit option and the prior state is restored.

## Environment-specific runtime coverage

The Hyper-V vSwitch classify and active transport-mode IPsec integration
gates are runtime-covered in addition to the representative dual-stack paths
above. The vSwitch result requires a real Hyper-V switch data path with the
inbox WFP switch extension enabled. The IPsec result requires two peers,
protected TCP and UDP traffic, and matching bidirectional Quick Mode SAs on
both peers. Registration and compile contracts alone still do not satisfy
either gate. The topology, acceptance criteria, evidence artifacts, and
cleanup boundary are documented in
[`test/wfp/runtime/advanced`](runtime/advanced/README.md#hyper-v-vswitch-and-ipsec-evidence).

Fast layers remain introspection-only because Windows does not support static
filtering at those internal layers. IPsec policy layers remain
management-only; neither category is exposed as a callout-capable NTL layer.

WFP redirect records are a TCP accepted-socket handoff. UDP has no accepted
socket on which such a record could be propagated, so "non-TCP redirect
record propagation" is not a missing runtime gate; datagram proxying carries
its typed tuple context instead.
