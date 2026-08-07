# Kernel connect-redirect acceptance

This traffic-only fixture launches the product controller, waits for policy
readiness, drives IPv4/IPv6 loopback echo traffic through the driver's WSK
proxy, and validates opaque redirect-record counts, bidirectional relay bytes,
unavailable-origin fail-closed counters, policy removal, and direct recovery.
It contains no WFP management or device-control calls.

With no arguments it locates the sibling controller and owns a unique
temporary IPC directory. An explicit controller and IPC directory remain
available as `acceptance.exe <controller.exe> <ipc-directory>` for isolated
fixture debugging.
