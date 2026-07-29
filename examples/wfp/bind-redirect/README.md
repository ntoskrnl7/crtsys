# NTL WFP bind redirect

This sample proves typed `ALE_BIND_REDIRECT_V4` and
`ALE_BIND_REDIRECT_V6` policy without exposing a writable native
`FWPS_BIND_REQUEST0` to application code.

The controller installs two dynamic, application-scoped UDP filters. A bind
to loopback port zero is changed to a driver-owned fixed test port for each
address family. The driver maps an opaque policy selector to the complete
address/port target, applies writable layer data exactly once, and rejects an
unknown selector or non-UDP classify.

The redirected sockets remain open while the dynamic session is removed.
New binds must then receive ordinary ephemeral ports, proving that no
persistent policy was left behind.

Build with the same WDK/toolset arguments used by the other WFP samples. Only
install the generated driver package in a disposable test VM.
