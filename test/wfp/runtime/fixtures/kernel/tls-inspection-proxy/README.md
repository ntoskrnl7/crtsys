# Kernel TLS inspection acceptance fixture

This directory owns controlled origins, clients, malformed/idle traffic, and
judgments for the kernel TLS proxy. It launches the adjacent
`crtsys_wfp_kernel_tls_inspection_proxy_controller.exe`; the controller alone
configures the driver, provisions temporary machine certificates, and owns
the WFP policy.

The fixture proves HTTP/1.1 and HTTP/2 permit/block behavior, IPv4/IPv6,
SNI/ALPN, request/response transforms, malformed and timeout failure,
bounded capture records, explicit policy removal, direct restoration, and
cleanup. It then validates controller-exported statistics and prints the PASS
marker.

The fixture contains no WFP-management, driver IOCTL, or service-control
calls.
