# NTL WFP bind redirect

This sample proves typed `ALE_BIND_REDIRECT_V4` and
`ALE_BIND_REDIRECT_V6` policy without exposing a writable native
`FWPS_BIND_REQUEST0` to application code.

The controller installs two dynamic, application-scoped UDP filters for the
explicit `--application` executable. A bind
to loopback port zero is changed to a driver-owned fixed test port for each
address family. The driver maps an opaque policy selector to the complete
address/port target, applies writable layer data exactly once, and rejects an
unknown selector or non-UDP classify.

The example and runtime proof are separate executables. The example
`crtsys_wfp_bind_redirect_controller.exe` owns only WFP policy lifetime. The
controlled socket creator and PASS assertions live in
`test/wfp/runtime/fixtures/kernel/bind-redirect` and build as
`crtsys_wfp_bind_redirect_acceptance.exe`. The acceptance process starts the
controller with its own application path, waits for the ready signal, creates
the IPv4/IPv6 binds, requests controller shutdown, and then verifies that new
binds receive ordinary ephemeral ports. The fixture never calls a WFP policy
API and the controller never creates test network traffic.

Build with the same WDK/toolset arguments used by the other WFP samples. Only
install the generated driver package in a disposable test VM.
