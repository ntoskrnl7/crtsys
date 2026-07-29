# WFP TLS inspection proxy

[Korean walkthrough](./README.ko-KR.md)

This sample is the transport-independent TLS inspection foundation. It proves
that a selected outbound IPv4 TCP connection can be redirected by a small WFP
callout, terminated in user mode with Schannel, inspected as bounded HTTP/1.1
plaintext, and connected to the original destination through a separately
validated TLS session.

The default deterministic run uses a loopback origin:

1. the driver redirects only the selected destination port;
2. user mode recovers the original destination and WFP redirect record;
3. a bounded ClientHello probe retains every ciphertext byte and obtains SNI;
4. the SNI selects a short-lived per-host identity from a bounded cache;
5. `tls_framed_stream` produces one complete bounded HTTP/1.1 message;
6. `ntl::net::inspection` permits `ALLOW` and drops `BLOCKME`; and
7. closing the dynamic WFP session restores direct connectivity.

HTTP message boundaries come from validated `Content-Length` or final
`chunked` framing. TLS, certificate issuance, HTTP framing, and plaintext
policy remain in user mode. The kernel callout only enforces entry into the
proxy path.

For long-running browser inspection, temporary trust management, and HTML
logging, use
[`browser-https-inspection`](../browser-https-inspection).

## Certificate boundary

The executable creates a temporary private test CA so the deterministic
example is self-contained. Its controlled client validates issued leaves with
`certificate_authority_policy`, which uses a private chain engine. The
application does not write a Windows trust store. Generated leaf keys and the
temporary CA key container are removed on exit.

NTL does not generate or silently install a product interception root.
Production software must inject an authorized protected issuer, deploy trust
through an administrator-controlled mechanism, rotate and audit keys, disclose
inspection, and define bypass and failure policy.

## Build and run

```powershell
cmake -S examples\wfp\tls-inspection-proxy `
      -B artifacts\examples\wfp-tls-inspection-proxy -A x64
cmake --build artifacts\examples\wfp-tls-inspection-proxy `
      --config Release
```

Test-sign and load `crtsys_wfp_tls_inspection_proxy.sys`, then run the
controller from an elevated shell:

```powershell
.\crtsys_wfp_tls_inspection_proxy_app.exe
```

The success marker is `NTL WFP TLS inspection-proxy ok:`.

## Controlled live HTTPS proof

An optional Internet-dependent controller path inspects one caller-selected
DNS host without launching a browser:

```powershell
.\crtsys_wfp_tls_inspection_proxy_app.exe `
    --inspect-host $env:NTL_WFP_TEST_HOST
```

The caller must choose an organization-approved host; no public site is built
into the example. DNS results are bounded and tried one at a time. Each
redirect filter remains scoped to this executable, one candidate IPv4
address, TCP, and port 443.

The upstream Schannel connection uses normal system-chain and host-name
validation. A managed HTTPS issuer that does not publish reachable revocation
information can be tested explicitly with
`--allow-unavailable-revocation`. That mode asks Schannel to check the chain
excluding its root and ignore only missing/offline revocation information.
Untrusted chains, name mismatches, expired certificates, and positive
revocation results still fail. The runner under
[`test/wfp/runtime/https-live`](../../../test/wfp/runtime/https-live)
packages and executes this proof.

## Protocol boundary

The sample deliberately fails instead of guessing when SNI is unavailable.
It is intentionally the small deterministic HTTP/1.1 foundation. The
browser-facing example adds IPv6, negotiated WebSocket
`permessage-deflate`, bounded gzip/deflate/Brotli decoders, a complete
HTTP/2/HPACK multiplexed relay, and explicit QUIC-bypass prevention. The
separate [`http3-inspection`](../http3-inspection) example demonstrates the
decrypted-QUIC/QPACK provider contract. A transparent HTTP/3 product path
still supplies the QUIC terminator and any dynamic QPACK implementation.
Confirmed ECH without a recovered inner ClientHello, pinning rejection, and
unavailable mutual-TLS identity remain explicit fail-closed policy results.
Raw extension type `0xfe0d` is not by itself confirmation because GREASE ECH
uses the same wire shape.
