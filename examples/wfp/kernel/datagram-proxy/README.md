# WFP kernel datagram-proxy

[한국어 설명](./README.ko-KR.md)

This sample demonstrates bounded, bidirectional IPv4/IPv6 UDP tuple
translation. `ALE_FLOW_ESTABLISHED_V4/V6` owns typed per-flow state,
`DATAGRAM_DATA_V4/V6` redirects client datagrams to a local proxy, and
`OUTBOUND_IPPACKET_V4/V6` validates replies emitted by that proxy, copies them
into fresh bounded NBLs, restores the original remote tuple, and reinjects them
through the network-send path before the connected client receives them. The
policy is ephemeral and disappears when the controller exits.

The example and its acceptance traffic are deliberately separate:

- `crtsys_wfp_datagram_proxy` is the driver.
- `crtsys_wfp_datagram_proxy_controller` installs the real policy and keeps it
  active. It does not create sockets, generate traffic, or print PASS.
- `crtsys_wfp_datagram_proxy_acceptance` is built from
  `test/wfp/runtime/fixtures/kernel/datagram-proxy`. It owns dual-stack
  senders/receivers, verifies exclusive redirection and cleanup, and prints the
  acceptance result.
- `crtsys_wfp_datagram_proxy_fragmented_buffer_contract` is a test-only driver
  built when `BUILD_TESTING=ON`. Loading it in a disposable test VM validates
  every UDP-header split across two MDLs, cross-fragment edits, bounded copies,
  and cleanup. That synthetic NBL validation is not run from the product
  driver's load path.

Controller contract:

```text
--original-port <1..65535> --proxy-port <1..65535> --application <exe-path>
--ready-file <path> --stop-file <path> --stats-file <path>
[--duration-ms <100..300000>]
```

The controller creates `ready-file` only after the transactional policy is
active. The fixture then sends traffic, creates `stop-file`, waits for the
controller to write `stats-file`, and verifies that traffic returns to the
original destination after controller exit. The acceptance executable finds
the sibling controller by default or accepts `--controller <path>`.

Build:

```powershell
cmake -S examples\wfp\kernel\datagram-proxy `
      -B artifacts\examples\wfp-datagram-proxy -A x64
cmake --build artifacts\examples\wfp-datagram-proxy --config Debug
```

The supported scope is bounded dual-stack UDP destination redirection,
transparent reply-tuple restoration, loop prevention, and injection
ownership. The proxy bounds mappings, deferred PASSIVE work, packet size,
control data, and asynchronous injections; quota or allocation failure absorbs
the selected packet and increments diagnostics. IPsec and the proxy
application's content policy remain separate concerns.
