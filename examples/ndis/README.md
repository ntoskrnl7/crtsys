# NTL NDIS examples

[`lwf-monitor`](./lwf-monitor/README.md) is the first NDIS reference. It
keeps forwarding ownership inside `ntl::ndis`, exposes callback-scoped NBL
and MDL data as bounded `ntl::net::scatter_view` values, reports offload
metadata without
rewriting it, and exercises a copied-payload TCP reassembler.

WFP remains the preferred layer for process, connection, stream, and
application-content policy. Use NDIS when Ethernet/NBL/NIC behavior is the
actual requirement.
