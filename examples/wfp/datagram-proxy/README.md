# WFP datagram-proxy

[한국어 설명](./README.ko-KR.md)

This driver/controller pair redirects one selected outbound IPv4 and IPv6 UDP
destination port to another dual-stack loopback port. It covers the essential WDK
`ddproxy` mechanism in a bounded, observable scenario:

1. `ALE_FLOW_ESTABLISHED_V4/V6` associates typed state with each UDP flow.
2. `DATAGRAM_DATA_V4/V6` clones the outbound NBL.
3. `cloned_packet::rewrite_udp_destination_port()` changes the UDP header.
4. `transport_injector` reinjects the clone.
5. The original datagram is blocked and absorbed.

At driver load, a synthetic UDP header split across two MDLs verifies that
the shared `scatter_view` editor can rewrite a field crossing the fragment
boundary. The same contract checks bounded deep-copy success and rejection
before the real injector and callouts are created.

The policy is dynamic and transactional. Closing the controller removes the
provider, sublayer, callouts, and filters together.

The supported scope is outbound dual-stack UDP destination-port redirection
and injection ownership. Transparent request/reply tuple restoration, IPsec,
and a user-mode proxy service require additional policy and adapters.

Build:

```powershell
cmake -S examples\wfp\datagram-proxy `
      -B artifacts\examples\wfp-datagram-proxy -A x64
cmake --build artifacts\examples\wfp-datagram-proxy --config Release
```
