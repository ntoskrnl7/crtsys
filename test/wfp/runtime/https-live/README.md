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
Workstation and can run four checks in one guest-operations session. The
browser check observes a browser that the VM operator already opened; it does
not start, stop, or configure a browser:

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

Every script that installs a driver or writes a machine certificate store
requires two deliberate guest proofs. Create the sentinel once inside the
disposable VM and pass the acknowledgement switch to direct suite commands:

```powershell
Set-Content C:\crtsys-disposable-test-guest.sentinel `
    'CRTSYS_DISPOSABLE_TEST_GUEST' -NoNewline
```

The scripts never create this sentinel. `Run-WfpHttpsVmAcceptance.ps1`
verifies it before changing the guest and requires the browser URL from the
caller; no public host is selected by default.

## Controlled-host check

```powershell
.\Run-WfpHttpsLiveTest.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -HostName $env:NTL_WFP_TEST_HOST `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

The controller tries distinct IPv4 DNS results, validates the origin with
Schannel, and compares proxy-observed plaintext with the controlled client
response. `-AllowUnavailableRevocation` tolerates only unavailable/offline
revocation data; it does not accept untrusted, expired, name-mismatched, or
positively revoked certificates.

## Browser inspection

Run from an elevated PowerShell in the copied package:

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Urls @($inspectionUrl) `
    -RequireQuicBlockedFallback `
    -LogDirectory C:\crtsys-wfp-https\browser-log `
    -DurationSeconds 90 `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

Before starting the wrapper, open the browser normally and leave that process
running. Do not supply a temporary profile or test flags. The wrapper finds
the executable path (Edge by default), starts only the sample driver and app,
waits for readiness, and temporarily trusts the generated inspection CA. It
never launches or terminates the browser and never supplies profile, feature,
certificate, QUIC, ECH, or logging arguments. Navigate in the already-open
window while the observation interval is active.

`-Urls` lists expected captures; it does not navigate the browser. If it is
omitted, at least one fresh inspected HTML response is still required.
`-RequireQuicBlockedFallback` requires all of the following:

- application-scoped IPv4 and IPv6 UDP/443 native block filters pass the
  bounded WFP inventory check;
- each filter ID printed by the runtime exists in the inventory from that
  same run;
- a WFP `classify_drop` net event for the observed browser, UDP/443, and one
  of those exact native filter IDs is observed; and
- fresh inspected HTML is captured over TCP, including every host supplied
  through `-Urls`.

The evidence directory includes `wfp-policy-diagnostics.log`,
`browser-transport-evidence.json`, the proxy logs, and captured HTML. Browser
NetLog and callout `action_write` counters are deliberately not acceptance
evidence. A run with no matching UDP/443 drop event is inconclusive and fails
the assertion instead of claiming that QUIC fallback occurred.

Without `-DurationSeconds`, browse manually and press Enter to stop. A timed
run observes for the full requested interval; it does not stop after the first
capture. Omitting the assertion switch does not change runtime policy, but it
allows a run with no observed UDP challenge to report `NOT_OBSERVED` instead
of failing.

The normal driver redirects application-scoped IPv4/IPv6 TCP 443 to the
Schannel proxy and blocks UDP 443. Chromium does not accept a custom
inspection CA for QUIC, so redirecting unchanged Edge to a private-CA QUIC
server ends with TLS `certificate_unknown`. Blocking UDP makes Edge use the
inspected TCP fallback without changing browser settings.

The stock-browser path does not depend on a managed browser trust policy or a
site-specific certificate exception. This does not generalize to products
that install their own managed client or integrate with a browser.

## Managed-client HTTP/3 inspection

This path is separate from the WFP browser policy. It does not load a driver,
launch or configure a browser, or write an inspection CA to a Windows trust
store:

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Url $inspectionUrl `
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

For `Run-WfpHttpsVmAcceptance.ps1`, the operator must open the ordinary
browser before starting the runner and navigate to `-BrowserUrl` while the
guest browser observation interval is active. `-BrowserUrl` is an expected
capture, not a browser-launch instruction.

The TCP path supports bounded HTTP/1.1, multiplexed HTTP/2, and WebSocket
`permessage-deflate`. HTTP/1.1, HTTP/2, and ordinary HTTP/3 responses use the
shared bounded gzip, zlib `deflate`, and Brotli decoders where applicable.
Captured `.html` files are server response bodies, not rendered DOM snapshots.

The evidence parser and the no-browser-mutation rule have offline contracts:

```powershell
.\Test-WfpBrowserTransportEvidenceContract.ps1
.\Test-WfpBrowserWrapperContract.ps1
```

The latter rejects browser launch/termination, disposable profiles, NetLog,
certificate-bypass, QUIC/ECH feature, and host-mapping arguments if they are
reintroduced into the ordinary-browser wrapper or VM runner.

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
