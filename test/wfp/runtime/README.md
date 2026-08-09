# WFP runtime acceptance

Runtime tests are grouped by observable sample behavior. A fixture name says
what network result it proves.

| Sample | Runtime gate |
| --- | --- |
| [`ale-connect-block`](./ale-connect-block) | Selected outbound IPv4 TCP connection is denied with `WSAEACCES`; session removal restores it; persistent manifest reconcile, controller-close survival, health, and explicit uninstall are verified; the driver loads/unloads under Driver Verifier |
| [`advanced`](./advanced) | Dual-stack datagram redirect, delayed async inspection, flow/stream telemetry, UDP content verdicts, framed-TCP content verdicts, local TCP connect redirection, bind redirection, and capability-honest IPsec/MAC/vSwitch/fast/endpoint-closure observation run while the selected drivers are targeted together by Driver Verifier; stream-edit also exercises IOCP read/write/cancel/EOF and dynamic framing |
| [`https-live`](./https-live) | Internet-dependent host inspection that observes an already-running ordinary browser without launching, terminating, re-profiling, or adding browser flags; IPv4/IPv6 TCP HTTPS responses are logged as bounded HTML and native WFP UDP/443 filters are verified by same-run inventory and classify-drop evidence |

The [advanced acceptance guide](./advanced/README.md#hyper-v-vswitch-and-ipsec-evidence)
records the additional topology, evidence, and cleanup requirements for real
Hyper-V vSwitch classify and active transport-mode TCP/UDP IPsec runs.

Each VM gate takes its VM path, credentials, and staging directories from
parameters. The operator preboots the disposable guest and preconfigures the
selected Driver Verifier targets. The runners never reboot, reset, or revert
the VM and never mutate Driver Verifier; they only verify the before/after
state. Any suite that installs a driver service or certificate also requires
an explicit acknowledgement and a caller-created disposable-guest sentinel.
No test is tied to one checkout, user account, or VM name.
