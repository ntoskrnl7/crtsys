# WFP runtime acceptance

Runtime tests are grouped by observable sample behavior. A fixture name says
what network result it proves.

| Sample | Runtime gate |
| --- | --- |
| [`ale-connect-block`](./ale-connect-block) | Selected outbound IPv4 TCP connection is denied with `WSAEACCES`, dynamic policy removal restores it, and the driver loads/unloads under Driver Verifier |
| [`advanced`](./advanced) | Datagram redirect, delayed async inspection, flow/stream telemetry, inline and deferred stream replacement, dual-stack local TCP connect redirection, dual-stack bind redirection, dynamic-SNI two-leg Schannel inspection, and separate UDP and framed-TCP content filters run while the selected drivers are targeted together by Driver Verifier; stream-edit also exercises IOCP read/write/cancel/EOF and dynamic framing |
| [`https-live`](./https-live) | Internet-dependent host inspection and an isolated Edge profile whose IPv4/IPv6 TCP HTTPS responses are decrypted and logged as bounded HTML files; browser UDP 443 is blocked by WFP so unavailable QUIC inspection cannot be bypassed |

Each VM gate takes its VM path, credentials, staging directories, and
pre-existing Verifier state from parameters. No test is tied to one checkout,
user account, or VM name.
