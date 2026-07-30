# Live HTTPS and browser inspection tests

These Internet-dependent tests are separate from deterministic WFP unit and
compile tests. They depend on DNS, public-site behavior, and any authorized
corporate HTTPS filter. They do not request a reboot or change Driver
Verifier.

The directory also contains a separate, deterministic and driverless HTTP/3
acceptance. It uses real msh3/MsQuic H3 on both loopback legs and does not
depend on the Internet, a browser, WFP, or trust-store writes:

```powershell
.\Prepare-ControlledHttp3Artifacts.ps1
.\Start-ControlledHttp3EndToEnd.ps1 `
    -PackageRoot ..\..\..\..\artifacts\controlled-http3-staging `
    -Concurrency 8
```

`Run-ControlledHttp3VmAcceptance.ps1` copies only that minimal package to the
VM. It additionally compares Driver Verifier, boot time, root stores,
ephemeral keys, WFP services, crash events, dumps, and remaining processes.
See [the Korean controlled-H3 guide](./CONTROLLED-HTTP3-README.ko-KR.md).

`Run-WfpHttpsVmAcceptance.ps1` copies one portable package to VMware
Workstation and can run four checks in one guest-operations session:

1. controlled-host TCP redirect and plaintext equality;
2. normal browser HTTPS inspection with captured HTML;
3. fail-closed WFP UDP/443 policy with normal browser TCP fallback; and
4. optional managed-client HTTP/3 inspection with application-owned trust.

The script removes temporary CA trust and driver services, compares Driver
Verifier settings before and after the run, checks for new crash events and
dumps, and copies an evidence archive under `artifacts`. A separate guest
post-check rejects any remaining sample service, process, or inspection CA.

## Prepare the portable package

On the development machine:

```powershell
.\Prepare-WfpHttpsLiveArtifacts.ps1
```

This builds and test-signs the `tls-inspection-proxy` and
`browser-https-inspection` x64 drivers and stages the drivers, applications,
INFs, signing certificates, pinned msh3/MsQuic runtime DLLs, notices, and
scripts under `artifacts\wfp-https-live-staging`.

The guest must already permit the test-signing policy used by the environment.
Driver-signing certificates and the temporary HTTPS inspection CA are
different certificates. The acceptance runner installs only the required
public certificates and removes its temporary inspection CA.

## Controlled-host check

```powershell
.\Run-WfpHttpsLiveTest.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -HostName $env:NTL_WFP_TEST_HOST
```

The controller tries distinct IPv4 DNS results, validates the origin with
Schannel, and compares proxy-observed plaintext with the controlled client
response. `-AllowUnavailableRevocation` tolerates only unavailable/offline
revocation data; it does not accept untrusted, expired, name-mismatched, or
positively revoked certificates.

## Browser inspection

Run from an elevated PowerShell in the copied package:

```powershell
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Urls @('https://www.google.com/') `
    -RequireQuicBlockedFallback `
    -LogDirectory C:\crtsys-wfp-https\browser-log `
    -DurationSeconds 90
```

The script starts the sample driver and app, waits for readiness, temporarily
trusts the generated inspection CA, and opens an isolated Edge profile. It
does not disable QUIC, force QUIC, ignore certificate errors, or change Edge
ECH policy. `-RequireQuicBlockedFallback` requires all of the following:

- the active provider, sublayer, callouts, filter action, and exact
  application/UDP/443 conditions pass the bounded WFP policy diagnostic;
- the kernel callout reports at least one matching classify with action-write
  rights and a block decision;
- Edge NetLog contains no direct target-host QUIC session that received and
  authenticated public packets; and
- the requested host still produced inspected HTML over TCP.

The evidence directory includes `wfp-policy-diagnostics.log`,
`quic-telemetry.json`, the original `edge-netlog.json`, and one
`quic-policy-<host>.json` verdict. A run with no UDP/443 classify is
inconclusive and fails the assertion instead of claiming that QUIC was
blocked.

Without `-DurationSeconds`, browse manually and press Enter to stop. Omitting
the assertion switch does not change runtime behavior.

The normal driver redirects application-scoped IPv4/IPv6 TCP 443 to the
Schannel proxy and blocks UDP 443. Chromium does not accept a custom
inspection CA for QUIC, so redirecting unchanged Edge to a private-CA QUIC
server ends with TLS `certificate_unknown`. Blocking UDP makes Edge use the
inspected TCP fallback without changing browser settings.

The acceptance investigation also tested Edge's managed `CACertificates`
trust policy. Edge validated the generated `www.google.com` chain with
`cert_status=0`, but still reported `is_issued_by_known_root=false` to its QUIC
proof verifier and rejected the connection. The policy is therefore not used
by this runtime. This result describes the stock-browser path only; it does
not generalize to products that install their own managed client or integrate
with a browser.

## Managed-client HTTP/3 inspection

This path is separate from the WFP browser policy. It does not load a driver,
launch or configure a browser, or write an inspection CA to a Windows trust
store:

```powershell
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Url 'https://www.google.com/' `
    -LogDirectory C:\crtsys-wfp-https\managed-http3-log
```

The NTL client retains the requested SNI and `:authority` while connecting to
an explicit loopback inspection endpoint. It validates the endpoint against
the wrapper-supplied CA in memory. The wrapper asserts a real downstream
HTTP/3 request and captured HTML.

The origin leg prefers and verifies HTTP/3. External QUIC transport,
connection, or timeout failures may retry with normally validated TLS/TCP;
the per-request event records whether the actual upstream was `h3`, `h2`, or
`http/1.1`. Certificate, mTLS, and request-validation failures do not
fallback.

Add `-IncludeManagedHttp3` to `Run-WfpHttpsVmAcceptance.ps1` to run this check
after the stock-browser fallback check in the same VM session.

The TCP path supports bounded HTTP/1.1, multiplexed HTTP/2, and WebSocket
`permessage-deflate`. HTTP/1.1, HTTP/2, and ordinary HTTP/3 responses use the
shared bounded gzip, zlib `deflate`, and Brotli decoders where applicable.
Captured `.html` files are server response bodies, not rendered DOM snapshots.

## Isolated SPKI diagnostic

`Start-BrowserHttp3SpkiDiagnostic.ps1` is retained only as a controlled
transport diagnostic. It does not load WFP. It maps its disposable browser
session to loopback, forces QUIC in that session, and supplies one exact
ephemeral SPKI exception. It can distinguish browser QUIC certificate-policy
problems from WFP redirect problems, but it is not the product-shaped path and
is not part of the normal browser acceptance result.

## Security and unsupported boundaries

WFP scopes by executable path, not profile. Use a dedicated VM because every
process using the selected browser executable is in scope while filters are
active. Captured content may be confidential.

An existing authorized corporate HTTPS filter must have its CA trusted by
Windows for the proxy's origin validation. That trust does not grant NTL keys
for arbitrary ECH, bypass certificate pinning, or choose an origin mTLS
identity.

The runtime has explicit providers for ECH frontend results, downstream
pinning knowledge, and exact-SNI origin client certificates. Arbitrary ECH
still requires the matching ECH private configuration and a TLS frontend that
owns decryption and termination. Pinning cannot be bypassed. The pinned msh3
backend does not expose the raw QUIC stream and Datagram callbacks required
for live extended CONNECT/WebTransport, even though the bounded NTL protocol
contracts are independently tested.
