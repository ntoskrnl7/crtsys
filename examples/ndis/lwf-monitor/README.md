# NTL NDIS LWF monitor

This sample is a pass-through NDIS lightweight filter reference.

`ntl::ndis::lightweight_filter<Module>` owns registration, attach, restart,
pause, detach, send/complete, receive/return, OID, status/PnP, and pass-through
ordering.
The module receives only callback-scoped views. It cannot retain or complete
the original NBL and cannot accidentally omit the matching forward, complete,
or return operation.

The sample reports:

- attached modules and restart/pause lifecycle;
- send, completion, and receive NBL/byte counts;
- checksum, LSO, RSC, VLAN, and receive-hash metadata; and
- transparent regular/direct OID request forwarding, completion, and cancel;
- status plus device/network PnP observation and propagation;
- metadata preservation contexts released at send completion or receive
  return; and
- a load-time bounded TCP reassembly contract covering out-of-order data,
  simulated loss/retransmission, overlap rejection, FIN, and 32-bit sequence
  wraparound; and
- load-time forced contracts for immediate and pending lower-edge OID results,
  cancel ordering, and `NDIS_RECEIVE_FLAGS_RESOURCES` immediate receive return.

The controller creates outbound UDP traffic and ICMP request/reply traffic with
the selected route's external next hop, then requires both send and receive
counters to advance. Installation must use the network component INF
(`netcfg -l ... -c s
-i crtsys_ntl_lwf_monitor`), not a bare service. Uninstall with `netcfg -u
crtsys_ntl_lwf_monitor`.

Install and run this driver only in a disposable VM. The sample does not block,
clone, originate production traffic, or rewrite packet metadata. The compile
contract separately exercises the mutation API; it restores only fields the
filter changed and preserves miniport LSO completion output.

Direct OID, device-PnP, native cancel, and resource-constrained receive counts
may remain zero when the selected adapter stack does not naturally issue those
callbacks. Their registration and common forwarding mechanics are still
compiled, and the deterministic edge contracts run when the driver loads.
