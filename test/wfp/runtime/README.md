# WFP runtime acceptance

Runtime tests are grouped by observable sample behavior. A fixture name says
what network result it proves.

| Sample | Runtime gate |
| --- | --- |
| [`ale-connect-block`](./ale-connect-block) | Selected outbound IPv4 TCP connection is denied with `WSAEACCES`; session removal restores it; persistent manifest reconcile, controller-close survival, health, and explicit uninstall are verified; the driver loads/unloads under Driver Verifier |
| [`advanced`](./advanced) | Dual-stack datagram redirect, delayed async inspection, flow/stream telemetry, UDP content verdicts, framed-TCP content verdicts, local TCP connect redirection, bind redirection, and capability-honest IPsec/MAC/vSwitch/fast/endpoint-closure observation run while the selected drivers are targeted together by Driver Verifier; stream-edit also exercises IOCP read/write/cancel/EOF and dynamic framing |
| [`https-live`](./https-live) | Internet-dependent host inspection and an isolated Edge profile whose IPv4/IPv6 TCP HTTPS responses are decrypted and logged as bounded HTML files; browser UDP 443 is blocked by WFP so unavailable QUIC inspection cannot be bypassed |

Each VM gate takes its VM path, credentials, staging directories, and
pre-existing Verifier state from parameters. No test is tied to one checkout,
user account, or VM name.
