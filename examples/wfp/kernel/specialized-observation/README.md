# WFP kernel specialized-observation

[한국어 설명](./README.ko-KR.md)

This sample models specialized WFP layers: endpoint closure, MAC frame,
vSwitch frame, fast transport, and IPsec management layers. The driver
registers observation-only callouts only for the callout-capable
endpoint/MAC/vSwitch layers. The controller introspects fast and IPsec
management layers without pretending that they accept static callouts.

Responsibilities are separated as follows:

- `crtsys_wfp_specialized_observation` is the driver.
- `crtsys_wfp_specialized_observation_controller` validates layer
  availability, opens the typed statistics device, installs observation
  policy for the supplied application, and records before/after snapshots.
- `crtsys_wfp_specialized_observation_acceptance` is built from
  `test/wfp/runtime/fixtures/kernel/specialized-observation`. It creates the
  observed IPv4/IPv6 endpoints and judges the resulting counters.

Controller contract:

```text
--application-path <absolute executable path>
--ready-file <path> --stop-file <path> --stats-file <path>
[--duration-ms <100..300000>]
```

Passing the fixture executable path is important: endpoint-closure filters are
application-scoped, so the controller must install policy for the process that
actually creates the sockets. The acceptance executable launches the sibling
controller by default or accepts `--controller <path>`. Environment-dependent
MAC and vSwitch gates can additionally specify:

```text
--traffic-target <reachable IPv4 address>
--traffic-duration-ms <100..300000>
--require-mac <true|false>
--require-vswitch <true|false>
```

The target must answer ICMP echo. Ordinary runs require only deterministic
dual-stack endpoint signals. `--require-mac true` additionally requires both
MAC directions, while `--require-vswitch true` requires both vSwitch
directions. A requested layer with no classify indication fails the run.

```powershell
cmake -S examples\wfp\kernel\specialized-observation `
      -B artifacts\examples\wfp-specialized-observation -A x64
cmake --build artifacts\examples\wfp-specialized-observation --config Debug
```

Zero counters for optional MAC/vSwitch activity mean the current machine did
not exercise that capability. They are accepted only by the default endpoint
gate; an explicit semantic requirement turns them into a hard failure.
