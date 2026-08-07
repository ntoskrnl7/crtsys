# Kernel connect-redirect controller

[Korean](./README.ko-KR.md)

The driver owns the dual-stack WSK proxy. The controller owns only the typed
WFP redirect policy, driver counter queries, lifecycle IPC, and statistics.
The acceptance proves IPv4 and IPv6, opaque WFP redirect records, relay in
both directions, fail-closed origin failure, and policy removal.

Each accepted pair runs inside nested `io::with_async_transport` owners and
uses `kernel::join_bidirectional` for the two relay directions. The sample has
no provider/stream declaration-order rule and no manual callback-drain
sequence; early return, cancellation, and either relay failure close admission
and join both transports before the owning operation completes.

```text
crtsys_wfp_kernel_connect_redirect_controller.exe <origin-port> <ipc-directory>
```

It writes `controller.ready` after querying valid kernel listener ports and
installing both filters. `stop.request` removes policy; final WSK counters are
written to `controller.stats`, including successfully captured opaque redirect
records.

Socket listeners, clients, traffic, and PASS assertions are in
`test/wfp/runtime/fixtures/kernel/connect-redirect`. The product build emits
`crtsys_wfp_kernel_connect_redirect_acceptance.exe` next to the controller.
