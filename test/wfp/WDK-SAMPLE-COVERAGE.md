# WDK WFP sample coverage

This document maps the reusable mechanisms in
`Windows-driver-samples/network/trans` to `ntl::wfp`. Coverage means that the
mechanism has a typed API and compile contract. Runtime status is stated
separately; a compile-covered advanced path is not reported as runtime-tested.

## Coverage matrix

| Microsoft sample | Reusable mechanisms | NTL surface | Evidence/status |
| --- | --- | --- | --- |
| `ddproxy` | ALE flow discovery, typed per-flow proxy state, datagram classify, NBL clone, block/absorb, asynchronous reinjection | `ale_flow_established_v4/v6`, `datagram_data_v4/v6`, `flow_target`, `add_flow_context`, `cloned_packet`, `transport_injector` | `/W4 /WX`; end-to-end UDP redirect passed 20 iterations under Driver Verifier; a load-time NBL fixture also edits a UDP field split across MDLs |
| `inspect` | ALE pending operation, transport packet retention, asynchronous worker decision, clone/reinject, unload drain | `pended_operation`, `referenced_packet`, `cloned_packet`, network/transport injectors with rundown | `/W4 /WX`; delayed permit/block and policy-removal recovery passed 20 iterations under Driver Verifier |
| `msnmntr` | flow-established observation, stream flow context, monitor-only stream classification, flow-delete cleanup | flow-established and stream tags, `inspection_filter_builder`, `add_stream`, `flow_target`, `stream_event` | `/W4 /WX`; flow/stream telemetry passed 20 iterations under Driver Verifier; load-time coroutine reader covers fragmentation, timeout, cancel, EOF, competing-reader, and limit paths |
| `stmedit` inline | stream byte copy, permit/block byte ranges, need-more-data, drop connection | `stream_data_view`, `scatter_view`, `scan_bytes`, `stream_result`, `stream_filter_builder`, `stream_control_filter_builder`; user-mode `io_completion_context` and `async_socket` | `/W4 /WX`; split-send replacement and policy-removal restoration passed 20 iterations under Driver Verifier; the controller's real payload path uses IOCP coroutine exact-read/write-all operations, with cancellation and incomplete-EOF self-tests |
| User-mode content verdict | complete datagram retention; application-selected TCP framing; inbound defer/continue; bounded kernel-to-user delivery; typed permit/block result; timeout and queue backpressure | `content_view`, `udp_datagram_view`, `tcp_message_view`, `framing`, NTL RPC reliable notifications, `cloned_packet`, `transport_injector`, `stream_injection_site` | `/W4 /WX`; UDP and sample-protocol TCP permit/block plus restoration pass the runtime fixture; TCP block drops the whole flow; malformed, late, timed-out, and over-quota results are fail-closed |
| `stmedit` out-of-band | defer/continue, clone stream data, replacement injection, distinct stream-clone cleanup | `stream_result::defer`, `stream_injection_site`, `cloned_stream_data`, `stream_injector` | `/W4 /WX`; deferred clone/continue, split-boundary variable-length replacement, and bounded busy handling passed three iterations under Driver Verifier |
| `WFPSampler` basic action scenarios | layer registration, provider/sublayer/callout/filter graph, permit/block/continue/absorb | typed keys/layers, `callout_driver`, `decision`, transactional management | Representative `ALE_AUTH_CONNECT_V4` path runtime- and Driver-Verifier-tested |
| `WFPSampler` packet/flow/stream scenarios | transport/datagram layers, flow association, packet and stream injection | packet/flow/stream APIs above | Reusable mechanisms compile-covered; the sample's large scenario CLI is not reproduced |
| `WFPSampler` connect redirect | writable `FWPS_CONNECT_REQUEST`, local proxy PID/port, redirect loop tracking, optional original-destination context, redirect-record propagation | `ale_connect_redirect_v4/v6`, `connect_redirector`, `local_proxy_target`, `connect_redirect_filter_builder`, `redirected_connection`; user-mode `async_socket` relay | `/W4 /WX`; IPv4 and IPv6 TCP request/response proxying, loop-free outbound reconnect, coroutine relay, and policy-removal restoration passed three iterations under Driver Verifier |
| `WFPSampler` bind redirect | writable `FWPS_BIND_REQUEST`, dual-stack address/port rewrite, reservation token, previous-version loop guard | `ale_bind_redirect_v4/v6`, `bind_redirect_selector`, `local_bind_target_v4/v6`, `bind_redirector`, `bind_redirect_filter_builder` | `/W4 /WX`; IPv4 and IPv6 selected-port binding plus post-policy ephemeral restoration passed three iterations under Driver Verifier |
| User-mode TLS inspection proxy | connect redirect, accepted-leg TLS termination, original-destination TLS client leg, plaintext application framing and typed policy | WFP connect-redirect surface above; user-mode `tls_credentials`, `tls_stream`, `framing`, and `inspection` | `/W4 /WX`; Schannel client/server runtime contracts pass on x64/x86; redirected permit/block and direct-policy-removal restoration are in the advanced VM gate |
| Browser HTTPS inspection | application-scoped connect redirect, dynamic per-SNI identity, long-running two-leg TLS relay, bounded HTTP/1 HTML logging | WFP and user-mode TLS surfaces above, in an independent driver/app contract | `/W4 /WX`; Internet-dependent isolated-Edge proof is packaged and run by `test/wfp/runtime/https-live`, outside the deterministic Verifier gate |
| `WFPSampler` specialized scenarios | IPsec, MAC/frame, vSwitch, name-resolution cache, endpoint closure, fast layers | typed specialized layer tags and compile contracts | `/W4 /WX` compile-covered; no runtime fixture yet, so native WFP remains available beside NTL for mechanisms not represented by a focused adapter |

## ALE connect-block runtime result

The x64 `ale-connect-block` driver and controller were exercised in a
disposable Windows 11 x64 VM:

1. driver registration and dynamic policy installation succeeded;
2. the selected loopback TCP connection failed with `WSAEACCES`;
3. closing the dynamic session removed policy and the connection succeeded;
4. twenty repeated policy/install/remove iterations passed while Driver
   Verifier targeted the WFP driver; and
5. the VM's prior Verifier target list was restored and checked after reboot.

The repository acceptance scripts and logs are the repeatable evidence. This
result validates the common ALE lifecycle and unload path.

## Advanced runtime result

The x64 baseline advanced gate runs `datagram-proxy`, `async-inspection`,
`flow-monitor`, `stream-edit`, `connect-redirect`,
`tls-inspection-proxy`, `udp-content-filter`, and `tcp-content-filter`
together under the standard Driver Verifier profile. That baseline passed:

1. 20 controller iterations for each of the eight baseline samples;
2. one verified driver load and unload for every target;
3. the fragmented UDP NBL/MDL and bounded coroutine-reader load-time
   contracts;
4. the stream-edit controller's IOCP coroutine read/write/cancel/EOF
   plus dynamic framing self-test before its policy iterations;
5. the UDP content-filter malformed, late, timeout, and pending-limit
   fail-closed self-test plus the TCP content-filter malformed, timeout,
   late-permit, and flow-drop self-test before their separate iterations;
6. the TLS inspection-proxy's two Schannel legs, decrypted permit/block
   verdicts, unchanged trust store, `close_notify`, and direct TLS connection
   after dynamic policy removal;
7. zero crash/reboot-error events and zero new kernel dumps; and
8. removal of all temporary WFP Verifier targets followed by restoration and
   verification of the caller-supplied target list.

The extension gate then passed three Verifier iterations each for
stream-edit's deferred clone/continue and variable replacement,
connect-redirect's IPv4/IPv6 coroutine relay, and the new IPv4/IPv6
bind-redirect sample. A later browser HTTP/3 Verifier run exposed one
WFP-owned original-destination context still alive at immediate driver
unload. The managed UDP redirect now explicitly omits that unused context;
the same external-QUIC-idle failure then unloaded cleanly under Verifier with
zero new crash events and dumps. Ordinary TCP proxy paths continue to request
the context.

The fixture performs two guest OS restarts—one to activate the temporary
targets and one to restore the prior settings—and never resets or reverts the
VM. Paths, credentials, VM identity, guest staging, existing Verifier targets,
SDK/toolset, and iteration count remain parameters rather than repository
machine assumptions.

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

## Remaining runtime gates

The representative advanced v4 paths above are runtime-covered. The following
surfaces remain compile-only or outside the current scope and must not be
reported as production-runtime-covered:

- IPv6 variants of the datagram, async-inspection, flow-monitor, TLS, and
  content-filter fixtures;
- non-TCP redirect-record propagation to an accepted user-mode socket;
- IPsec, MAC/frame, vSwitch, fast-layer, and endpoint-closure scenarios.
