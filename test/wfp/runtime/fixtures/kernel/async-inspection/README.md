# Async-inspection acceptance

This fixture owns all listeners, client connects, timing assertions, PASS
output, and unload-race fan-out. It launches the product controller and uses
`controller.ready`, driver stop/restart acknowledgements, `release-policy`,
`policy.released`, `stop.request`, and `controller.stats` IPC. The race proves
pending-decision drain, fail-closed cancellation, driver reload, and policy
cleanup. It never installs WFP policy or opens the driver itself.

```text
crtsys_wfp_async_inspection_acceptance.exe <controller.exe> <ipc-directory>
crtsys_wfp_async_inspection_acceptance.exe <controller.exe> <ipc-directory> --unload-race <connection-count> <driver-service>
```
