# NTL NDIS LWF monitor

This sample is a read-only NDIS lightweight filter reference.

`ntl::ndis::lightweight_filter<Module>` owns registration, attach, restart,
pause, detach, send/complete, receive/return, and pass-through ordering.
The module receives only callback-scoped views. It cannot retain or complete
the original NBL and cannot accidentally omit the matching forward, complete,
or return operation.

The sample reports:

- attached modules and restart/pause lifecycle;
- send, completion, and receive NBL/byte counts;
- checksum, LSO, RSC, VLAN, and receive-hash metadata; and
- a load-time bounded TCP reassembly contract covering out-of-order data,
  overlap rejection, FIN, and 32-bit sequence wraparound.

The controller creates outbound UDP traffic and requires the send counters to
advance. Installation must use the network component INF (`netcfg -l ... -c s
-i crtsys_ntl_lwf_monitor`), not a bare service. Uninstall with `netcfg -u
crtsys_ntl_lwf_monitor`.

Install and run this driver only in a disposable VM. The sample does not
modify, block, clone, or originate production traffic.
