# User-mode WFP TLS inspection proxy

[한국어 설명](./README.ko-KR.md)

This sample contains the deployable side of a two-leg user-mode TLS proxy:

- `crtsys_wfp_tls_inspection_proxy.sys` redirects selected IPv4/IPv6 connects;
- `crtsys_wfp_tls_inspection_proxy_service.exe` owns the ephemeral WFP
  policy, recovers the original destination and redirect record, terminates
  Schannel with an SNI-selected bounded identity cache, mirrors ALPN on the
  upstream TLS leg, and runs bounded HTTP/1.1 or HTTP/2 transforms; and
- `crtsys_wfp_tls_inspection_proxy_acceptance.exe` is the separate controlled
  runtime fixture under
  `test/wfp/runtime/fixtures/user/tls-inspection-proxy`.

The example directory no longer contains its own loopback origin, controlled
client, malformed-input generator, or PASS verdict. Those belong to the
acceptance fixture. The service exposes bounded file IPC:

1. `--ready-file` means certificates, listeners, and policy are ready;
2. `--remove-policy-file` asks it to close the ephemeral WFP session;
3. `--policy-removed-file` confirms direct-connect testing is safe;
4. `--stop-file` ends the service; and
5. `--stats-file` contains protocol, transform, SNI, tuple, and failure
   counters.

The actual data path retains the original capabilities: dual-stack redirect
record recovery, two validated TLS legs, bounded ClientHello/SNI processing,
required `http/1.1` or `h2` ALPN, HTTP/1 framing, HTTP/2 frame/HPACK state,
request-header rewriting, HTML response rewriting, and fail-closed content
blocking.

The default HTTP/2 redirected session binds every transformed `:authority` to
the SNI that selected its one upstream TLS connection before forwarding. It
also rejects classic and Extended CONNECT before forwarding unless a tunnel
handler makes an explicit inspect or passthrough admission decision. Disabling the SNI
binding is reserved for a product that supplies its own explicit origin
coalescing policy.

## Reusable API and example boundary

The service does not rebuild a TLS/HTTP proxy connection itself. Public NTL
components own the repetitive product path:

- `redirected_tls_session_registry` bounds concurrent connections and drains
  their coroutine lifetimes without one detached thread per connection;
- `redirected_tls_session` validates a bounded ClientHello, recovers the
  original WFP tuple plus exact process/application identity, performs the
  asynchronous original-destination connect, and establishes both Schannel
  legs with one selected ALPN; and
- `standard_redirected_tls_inspection` supplies independent per-connection
  HTTP/1 and HTTP/2 framing, request association, HPACK/flow-control, Upgrade,
  and Extended CONNECT state around one protocol-neutral policy.

The files under `app` consequently have narrow roles:

- `inspection_policy.{hpp,cpp}` is the sample-specific policy only;
- `proxy_engine.{hpp,cpp}` adds sample diagnostics around the reusable
  dispatcher; and
- `main.cpp` owns listener, ephemeral WFP policy, certificates, and service
  lifetime.

Synthetic traffic and frame assertions live only under `test` or the separate
runtime fixture. In particular, the HTTP/2 encoder/assertion contract is not
linked into the service.

A decision is not limited to the body. Every HTTP/1 or HTTP/2 adapter supplies
the same staged `inspection_context_view`, so a rule can combine method, target,
headers, body, TLS, process identity, application identity, and original WFP
tuple:

```cpp
namespace c = ntl::net::http::condition;

policy.requests()
    .at_message_complete()
    .when(c::all_of(
        c::method_is("POST"),
        c::path_is("/inspect"),
        c::original_destination_port_is(443),
        c::any_of(c::header_is("x-ntl-block", "1"),
                  c::complete_body_contains("BLOCKME"))))
    .decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::block;
    });
```

`block` produces a bounded semantic 403 response for HTTP/1 and HTTP/2;
`drop_flow` closes the transport. Malformed or over-limit input follows the
configured fail-closed policy.

## Certificate boundary

The controlled service creates a temporary private CA and publishes only the
origin leaf in `LocalMachine\My` so the separate fixture process can host the
origin while the service is alive. The CA is exported to the private IPC
directory and both client and upstream validation use
`certificate_authority_policy`; no trusted root is installed. The leaf, leaf
keys, CA key, and IPC directory are removed at process/fixture teardown.

Production software must inject an administrator-authorized protected issuer
and define deployment, rotation, audit, disclosure, bypass, and failure
policy. NTL does not silently create or trust a product interception root.

## Build and validate

```powershell
cmake -S examples\wfp\user\tls-inspection-proxy `
      -B artifacts\examples\wfp-user-tls -A x64 -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-user-tls --config Debug
ctest --test-dir artifacts\examples\wfp-user-tls `
      -C Debug --output-on-failure
```

After test-signing/loading the driver in a disposable VM, run the adjacent
acceptance executable from an elevated shell:

```powershell
.\crtsys_wfp_tls_inspection_proxy_acceptance.exe
```

It launches the sibling service, proves HTTP/1.1 and HTTP/2 permit/block and
request/response transforms over IPv4 and IPv6, sends malformed TLS on both
families, verifies the real WFP process/application identity handoff, requests
policy removal, proves direct TLS connectivity, stops the service, and
validates its statistics. Success begins with:

```text
NTL WFP TLS inspection acceptance PASS:
```

The install-free CTests validate the same HTTP/1 and HTTP/2 transform policy
without loading a driver. They also validate the pointer-free redirect handoff
record and the bounded public redirected-session contract.

## Controlled live HTTPS proof

The previous caller-selected Internet proof is preserved as a separate
`https-live` acceptance executable, so its generated client and verdict do
not live in the proxy service:

```powershell
.\crtsys_wfp_tls_inspection_proxy_live_acceptance.exe `
    --inspect-host $env:NTL_WFP_TEST_HOST
```

The caller chooses an approved host; none is hard-coded. Its filters remain
scoped to this executable, one bounded IPv4 DNS candidate, TCP, and port 443.
The upstream connection uses normal system chain/name validation. The optional
`--allow-unavailable-revocation` permits only missing/offline revocation
information; untrusted, expired, name-mismatched, or positively revoked
certificates still fail.

The focused proxy handles one bounded HTTP exchange per TLS connection.
Persistent browser traffic, WebSocket, HTTP/3, ECH, and managed certificate
deployment remain in their dedicated examples and product policy layers.
