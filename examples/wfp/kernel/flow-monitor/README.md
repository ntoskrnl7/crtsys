# WFP kernel flow-monitor

[한국어 설명](./README.ko-KR.md)

This observation-only sample attaches typed state to selected outbound
IPv4/IPv6 TCP flows and counts stream indications, bytes, missed bytes, and
flow lifetime events without changing traffic.

The runtime boundary is explicit:

- `crtsys_wfp_flow_monitor` is the driver.
- `crtsys_wfp_flow_monitor_controller` opens the control device, snapshots
  driver statistics, installs the two port-scoped policies, and writes the
  final snapshot. It does not own clients, servers, load generation, or PASS.
- `crtsys_wfp_flow_monitor_acceptance` is built from
  `test/wfp/runtime/fixtures/kernel/flow-monitor`. It creates the servers,
  generates IPv4/IPv6 traffic and load, and judges telemetry.

Controller contract:

```text
--ipv4-port <1..65535> --ipv6-port <1..65535>
--ready-file <path> --stop-file <path> --stats-file <path>
[--duration-ms <100..300000>]
```

Acceptance modes:

```powershell
crtsys_wfp_flow_monitor_acceptance.exe
crtsys_wfp_flow_monitor_acceptance.exe --load-test 10000 32
crtsys_wfp_flow_monitor_acceptance.exe --controller <path> --load-test 10000 32
```

The two load arguments are flows per address family and worker concurrency.
The fixture launches the controller, waits for ready, sends traffic, signals
stop, and evaluates the before/after statistics written by the controller.

```powershell
cmake -S examples\wfp\kernel\flow-monitor `
      -B artifacts\examples\wfp-flow-monitor -A x64
cmake --build artifacts\examples\wfp-flow-monitor --config Debug
```
