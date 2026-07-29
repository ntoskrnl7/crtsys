# WFP ALE connect-block sample

[한국어 설명](./README.ko-KR.md)

This sample temporarily blocks one outbound IPv4 TCP connection to a selected
loopback port. When its dynamic WFP policy session closes, the same connection
succeeds.

## Who does what

| Part | Responsibility |
| --- | --- |
| `crtsys_wfp_ale_connect_block.sys` | Registers the kernel callout and returns `block` when WFP classifies the selected connection |
| `crtsys_wfp_ale_connect_block_app.exe` | Starts a local TCP listener, installs temporary WFP policy, proves block, removes policy, and proves recovery |
| Windows Filtering Platform | Matches the app's filter and invokes the driver's callout at `ALE_AUTH_CONNECT_V4` |

The app owns policy; the driver owns the decision callback. Their shared
callout GUID connects the user-mode and kernel-mode registrations.

## Observable sequence

1. The app starts a TCP listener on `127.0.0.1:<port>`.
2. The app opens a dynamic WFP session.
3. In one transaction it adds provider → sublayer → callout → filter.
4. The filter matches outbound IPv4 TCP connections to `<port>`.
5. WFP calls the driver's typed `classify_event<ALE_AUTH_CONNECT_V4>`
   callback.
6. The driver returns `block`, so `connect()` fails with `WSAEACCES` (`10013`).
7. The dynamic session closes and removes all four policy objects.
8. The app repeats `connect()` and it succeeds.

The executable prints every stage as it runs. Its final success marker is:

```text
NTL WFP ale-connect-block ok: blocked_error=10013, restored_connect=success
```

## What this proves

- kernel callout registration and user-mode policy registration meet on the
  same typed layer and callout key;
- `ntl::wfp` applies action-write and clear-action-right rules;
- the selected connection is actually denied by the kernel callout; and
- dynamic policy leaves no persistent firewall objects after session close.

Continue with [`datagram-proxy`](../datagram-proxy) for UDP packet redirection,
[`async-inspection`](../async-inspection) for pended ALE decisions,
[`connect-redirect`](../connect-redirect) for a local TCP proxy, or
[`stream-edit`](../stream-edit) for TCP stream modification.

## Build and run

The driver must be loaded before the app installs a filter that refers to its
callout. Run the app elevated because it changes WFP policy.

```powershell
cmake -S examples\wfp\ale-connect-block `
      -B artifacts\examples\wfp-ale-connect-block -A x64
cmake --build artifacts\examples\wfp-ale-connect-block --config Release

sc.exe create crtsys_wfp_ale_connect_block type= kernel start= demand `
  binPath= C:\path\to\crtsys_wfp_ale_connect_block.sys
sc.exe start crtsys_wfp_ale_connect_block
.\crtsys_wfp_ale_connect_block_app.exe
sc.exe stop crtsys_wfp_ale_connect_block
sc.exe delete crtsys_wfp_ale_connect_block
```

For test signing, repeated execution, and Driver Verifier automation, use
[`test/wfp/runtime/ale-connect-block`](../../../test/wfp/runtime/ale-connect-block).
