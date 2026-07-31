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
| `msnmntr` | flow-established observation, stream flow context, monitor-only stream classification, flow-delete cleanup | flow-established and stream tags, `inspection_filter_builder`, `add_stream`, `flow_target`, `stream_event` | `/W4 /WX`; IPv4 and IPv6 flow/stream telemetry passed 20 iterations under Driver Verifier; load-time coroutine reader covers fragmentation, timeout, cancel, EOF, competing-reader, and limit paths |
| `stmedit` inline | stream byte copy, permit/block byte ranges, need-more-data, drop connection | `stream_data_view`, `scatter_view`, `scan_bytes`, `stream_result`, `stream_filter_builder`, `stream_control_filter_builder`; user-mode `io_completion_context` and `async_socket` | `/W4 /WX`; split-send replacement and policy-removal restoration passed 20 iterations under Driver Verifier; the controller's real payload path uses IOCP coroutine exact-read/write-all operations, with cancellation and incomplete-EOF self-tests |
| User-mode content verdict | complete datagram retention; application-selected TCP framing; inbound defer/continue; bounded kernel-to-user delivery; typed permit/block result; timeout and queue backpressure | `content_view`, `udp_datagram_view`, `tcp_message_view`, `framing`, NTL RPC reliable notifications, `cloned_packet`, `transport_injector`, `stream_continuation` | `/W4 /WX`; IPv4 and IPv6 UDP and sample-protocol TCP permit/block plus restoration passed 20 iterations under Driver Verifier; one layer-erased continuation queue safely resumes either stream family; TCP block drops the whole flow; malformed, late, timed-out, and over-quota results are fail-closed |
| `stmedit` out-of-band | defer/continue, clone stream data, replacement injection, distinct stream-clone cleanup | `stream_result::defer`, `stream_injection_site`, `cloned_stream_data`, `stream_injector` | `/W4 /WX`; deferred clone/continue, split-boundary variable-length replacement, and bounded busy handling passed three iterations under Driver Verifier |
| `WFPSampler` basic action scenarios | layer registration, provider/sublayer/callout/filter graph, permit/block/continue/absorb | typed keys/layers, `callout_driver`, `decision`, transactional management, `policy_session`, `policy_manifest` | Representative `ALE_AUTH_CONNECT_V4` path runtime- and Driver-Verifier-tested; persistent reconcile, controller-close survival, health, and explicit uninstall are runtime-tested |
| `WFPSampler` packet/flow/stream scenarios | transport/datagram layers, flow association, packet and stream injection | packet/flow/stream APIs above | Reusable mechanisms compile-covered; the sample's large scenario CLI is not reproduced |
| `WFPSampler` connect redirect | writable `FWPS_CONNECT_REQUEST`, local proxy PID/port, redirect loop tracking, optional original-destination context, redirect-record propagation | `ale_connect_redirect_v4/v6`, `connect_redirector`, `local_proxy_target`, `connect_redirect_filter_builder`, `redirected_connection`; user-mode `async_socket` relay | `/W4 /WX`; IPv4 and IPv6 TCP request/response proxying, loop-free outbound reconnect, coroutine relay, and policy-removal restoration passed three iterations under Driver Verifier |
| `WFPSampler` bind redirect | writable `FWPS_BIND_REQUEST`, dual-stack address/port rewrite, reservation token, previous-version loop guard | `ale_bind_redirect_v4/v6`, `bind_redirect_selector`, `local_bind_target_v4/v6`, `bind_redirector`, `bind_redirect_filter_builder` | `/W4 /WX`; IPv4 and IPv6 selected-port binding plus post-policy ephemeral restoration passed three iterations under Driver Verifier |
| User-mode TLS inspection proxy | connect redirect, accepted-leg TLS termination, original-destination TLS client leg, plaintext application framing and typed policy | WFP connect-redirect surface above; user-mode `tls_credentials`, `tls_stream`, `framing`, and `inspection` | `/W4 /WX`; Schannel client/server runtime contracts pass on x64/x86; redirected permit/block and direct-policy-removal restoration are in the advanced VM gate |
| Browser HTTPS inspection | application-scoped connect redirect, dynamic per-SNI identity, long-running two-leg TLS relay, bounded HTTP/1 HTML logging | WFP and user-mode TLS surfaces above, in an independent driver/app contract | `/W4 /WX`; Internet-dependent isolated-Edge proof is packaged and run by `test/wfp/runtime/https-live`, outside the deterministic Verifier gate |
| `WFPSampler` specialized scenarios | IPsec policy, MAC/frame, vSwitch, name-resolution cache, endpoint closure, fast-layer metadata | typed specialized layer tags, capability categories, and compile contracts | `/W4 /WX`; endpoint-closure IPv4/IPv6 callouts are runtime-tested under Driver Verifier; MAC/frame and vSwitch registrations are runtime-tested but require matching traffic to exercise classify; IPsec layers are management-only and fast layers are introspection-only rather than legal static-callout targets |

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
3. the fragmented UDP NBL/MDL and bounded coroutine-reader load-time
   contracts;
4. IPv4 and IPv6 redirect, delayed decision, telemetry, UDP verdict, and TCP
   verdict paths plus policy-removal restoration for both families;
5. the UDP content-filter malformed, late, timeout, and pending-limit
   fail-closed self-test plus the TCP content-filter malformed, timeout,
   late-permit, and flow-drop self-test before their separate iterations;
6. zero crash/reboot-error events and zero new kernel dumps; and
7. removal of all temporary WFP Verifier targets followed by restoration and
   verification of the caller-supplied target list.

The same source state built all 13 WFP compile/example projects for x64 and
ARM64 in Debug and Release. The eight x64 semantic CTest contracts passed in
both configurations. The final same-boot VM audit found no WFP service,
controller process, test certificate, project WFP policy, crash event, or new
dump, and found no WFP driver left in the caller's restored Verifier target
list.

Separate extension gates passed stream-edit's deferred clone/continue,
variable replacement, and IOCP coroutine read/write/cancel/EOF contracts;
connect-redirect's IPv4/IPv6 coroutine relay; IPv4/IPv6 bind redirect; and
the TLS inspection proxy's two Schannel legs, decrypted permit/block verdicts,
unchanged trust store, `close_notify`, and direct TLS restoration. A later
browser HTTP/3 Verifier run exposed one
WFP-owned original-destination context still alive at immediate driver
unload. The managed UDP redirect now explicitly omits that unused context;
the same external-QUIC-idle failure then unloaded cleanly under Verifier with
zero new crash events and dumps. Ordinary TCP proxy paths continue to request
the context.

The full fixture uses three guest OS restarts: one to activate the normal
temporary targets, one to replace them with an independent Systematic Low
Resources Simulation boot, and one to restore the prior settings.
`-SkipLowResourcePass` reduces that to two. Manual restart mode is the default
so a test VM can be booted through Windows startup settings with driver
signature enforcement disabled; automatic restart is an explicit opt-in for
appropriately signed or test-signing-enabled environments. Manual mode uses a
temporary persistent Verifier configuration because Startup Settings can
consume a one-boot reservation before the operator-selected Windows boot;
the prior configuration is still restored explicitly. The fixture never
resets or reverts the VM. Paths, credentials, VM identity, guest staging,
existing Verifier targets, SDK/toolset, and iteration count remain parameters
rather than repository machine assumptions.

The normal Verifier baseline above has passed. The independent Low Resources
gate defaults to Systematic rule classes 19 and 36 on Windows 11, then enables
runtime injection independently for each sample. Each bounded injection pass
increments the Systematic test-pass counter so the next run reaches the next
failure site; every sample must report a nonzero injection count and then
recover successfully after runtime injection is disabled. The gate does not
pretend to exhaust an open-ended set of newly discovered call stacks. The preceding
normal-Verifier boot owns the baseline proof, while this boot owns the
reproducible allocation-failure, recovery, and cleanup proof. Randomized mode
remains an exploratory option rather than the default product gate.

## Safety properties

The public surface intentionally makes these invalid combinations
unrepresentable:

1. A callout or filter key cannot be used at a different layer.
2. A normal classify callback cannot mutate `FWPS_CLASSIFY_OUT0`.
3. A stream callback cannot return an ordinary `decision`, and an ordinary
   callback cannot return a `stream_result`.
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

## Remaining runtime gates

The representative dual-stack paths above, including the standalone Schannel
TLS proxy and endpoint closure, are runtime-covered. The following
environment-dependent surfaces must not be reported as exercised merely
because their registrations and compile contracts passed:

- MAC/frame classify with matching layer traffic;
- vSwitch classify with a Hyper-V virtual-switch topology; and
- IPsec policy integration with an active IPsec scenario.

Fast layers remain introspection-only because Windows does not support static
filtering at those internal layers. IPsec policy layers remain
management-only; neither category is exposed as a callout-capable NTL layer.

WFP redirect records are a TCP accepted-socket handoff. UDP has no accepted
socket on which such a record could be propagated, so "non-TCP redirect
record propagation" is not a missing runtime gate; datagram proxying carries
its typed tuple context instead.
