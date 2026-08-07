# User TLS inspection acceptance fixture

This directory owns only controlled runtime traffic and judgments for the
user-mode TLS proxy. It launches the adjacent
`crtsys_wfp_tls_inspection_proxy_service.exe`, waits for readiness, opens
IPv4/IPv6 TLS origins from the service-published short-lived leaf, and proves:

- HTTP/1.1 and HTTP/2 permit and block decisions on both address families;
- SNI/ALPN, request-header transforms, and HTML response transforms;
- malformed TLS rejection without origin delivery;
- explicit policy-removal acknowledgement and direct TLS restoration; and
- bounded service statistics before printing the PASS marker.

The fixture does not manage WFP, call driver IOCTLs, or mutate services. The
example service owns those privileged operations and the actual proxy data
path.
