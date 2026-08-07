# User connect-redirect acceptance

This fixture owns the loopback IPv4/IPv6 origins, clients, assertions, and
PASS output. It launches the product `*_proxy_service`, waits for
`controller.ready`, sends traffic, creates `stop.request`, and validates
`controller.stats`. It verifies original-destination capture, coroutine byte
counters, policy removal, unavailable-proxy fail-closed behavior, no origin
bypass, and recovery. It never opens a WFP session or configures a driver.
