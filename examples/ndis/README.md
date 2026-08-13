# NTL NDIS examples

[`lwf-monitor`](./lwf-monitor/README.md) is the first NDIS reference. It
keeps forwarding ownership inside `ntl::ndis`, exposes callback-scoped NBL
and MDL data as bounded `ntl::net::scatter_view` values, reports offload
metadata without
rewriting it, and exercises a copied-payload TCP reassembler.

`send_event` and `receive_event` can arm metadata preservation with
`try_preserve_metadata()`. Changes made through the returned mutable view are
tracked per field and restored at send completion or receive return. The LSO
slot is an exception on send completion because it contains miniport completion
output; that result is passed upward unchanged. The adapter clones and forwards
regular and direct OID requests, exposes callback-scoped request/completion
views, and propagates status plus device/network PnP notifications after a
read-only module observation. Synchronous OID interception remains outside
this NDIS 6.30 foundation.

WFP remains the preferred layer for process, connection, stream, and
application-content policy. Use NDIS when Ethernet/NBL/NIC behavior is the
actual requirement.
