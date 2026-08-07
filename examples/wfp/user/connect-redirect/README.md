# User-mode connect redirect proxy service

[Korean](./README.ko-KR.md)

This product example contains the WFP redirect driver and the real dual-stack
TCP proxy service. The service owns its proxy listeners, typed redirect policy,
original-destination recovery, coroutine relay, lifecycle IPC, and statistics.
The acceptance proves IPv4 and IPv6, opaque redirect records, relays in both
directions, and that policy restores direct connectivity.

```text
crtsys_wfp_connect_redirect_proxy_service.exe <origin-port-v4> <origin-port-v6> <ipc-directory>
crtsys_wfp_connect_redirect_proxy_service.exe --unavailable-proxy <origin-port-v4> <origin-port-v6> <ipc-directory>
```

After both proxy listeners and WFP filters are active it writes
`controller.ready`. Creating `stop.request` removes policy and drains the two
relays. Final byte counters are written to `controller.stats`.
`--unavailable-proxy` installs the same fail-closed policy against closed proxy
endpoints without generating traffic; the acceptance fixture drives and
observes that deployment state through IPC.

Controlled origins, clients, PASS assertions, and automatic traffic are kept
under `test/wfp/runtime/fixtures/user/connect-redirect`. The product build also
emits `crtsys_wfp_connect_redirect_acceptance.exe` next to the service; the
fixture contains no WFP policy code.

```powershell
cmake -S examples\wfp\user\connect-redirect `
      -B artifacts\examples\wfp-user-connect-redirect -A x64
cmake --build artifacts\examples\wfp-user-connect-redirect --config Debug
```
