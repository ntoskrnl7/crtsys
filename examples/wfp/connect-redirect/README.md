# WFP connect-redirect

This sample redirects one selected outbound IPv4 TCP destination to a local
user-mode proxy. The proxy captures the original destination and WFP's opaque
redirect records, opens the outbound leg in the required order, and relays
both directions with `co_await`.

The kernel callback has one mutation operation:

```cpp
return redirector.redirect(event, target);
```

That operation detects redirect loops, acquires and applies writable
`FWPS_CONNECT_REQUEST0` data, transfers the original endpoint context to WFP,
and blocks on native failure. The application uses:

```cpp
auto handoff = ntl::wfp::redirected_connection::capture(accepted);
SOCKET outbound = handoff.connect_original();
```

`connect_original()` attaches the opaque redirect records before connecting,
so the callout recognizes the outbound proxy leg and does not redirect it
again. Application code does not parse or manufacture those records.

The runtime proof starts an origin listener and a proxy listener in the same
process, installs a dynamic rule for only the origin port, and verifies:

1. the first client connection reaches the proxy;
2. the proxy observes the correct original destination;
3. coroutine relays preserve the complete request and response;
4. the outbound proxy leg reaches the origin without a redirect loop; and
5. removing the ephemeral policy restores a direct connection.

This is a transparent TCP byte-stream proxy foundation. For Schannel
termination and plaintext policy on the accepted socket, see
[`tls-inspection-proxy`](../tls-inspection-proxy).

Build:

```powershell
cmake -S examples\wfp\connect-redirect `
      -B artifacts\examples\wfp-connect-redirect -A x64
cmake --build artifacts\examples\wfp-connect-redirect --config Release
```
