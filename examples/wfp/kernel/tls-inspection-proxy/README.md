# Kernel WFP TLS inspection proxy

[한국어 설명](./README.ko-KR.md)

This sample keeps the real kernel TLS data path separate from its controlled
runtime fixture:

- `crtsys_wfp_kernel_tls_inspection_proxy.sys` owns WSK redirect-record
  recovery, kernel Schannel, ALPN, bounded HTTP/1.1 and HTTP/2 parsing,
  request/response transforms, blocking, and capture telemetry;
- `crtsys_wfp_kernel_tls_inspection_proxy_controller.exe` provisions the
  short-lived identity, configures the driver, owns the ephemeral WFP policy,
  and exports raw counter/capture evidence; and
- `crtsys_wfp_kernel_tls_inspection_proxy_acceptance.exe` lives under
  `test/wfp/runtime/fixtures/kernel/tls-inspection-proxy` and owns controlled
  origins, clients, malformed/idle traffic, judgments, and the PASS marker.

The fixture contains no WFP-management, driver IOCTL, or service-control
calls. Its bounded IPC sequence is `ready`, policy-removal request,
policy-removed acknowledgement, direct-connect proof, stop, then statistics.
This keeps the temporary certificate keys alive until the final direct proof.

The driver preserves the original IPv4/IPv6 tuple and opaque redirect record,
parses arbitrarily fragmented ClientHello, selects a machine-store identity by
SNI, terminates inbound kernel Schannel, and requires the same `http/1.1` or
`h2` ALPN on the system-validated upstream leg. Its common semantic policy:

- adds `x-ntl-inspected: 1` to requests;
- returns 403 for `X-NTL-Block: 1` or `BLOCKME` without opening upstream;
- forwards permitted traffic to the TLS origin; and
- appends `<!-- inspected by ntl -->` to HTML responses.

Each session runs inside an `io::with_async_transport` owner. That operation
keeps the WSK backend, stream, Schannel state, and callbacks alive together,
rejects new work after close begins, and joins every completion path before it
finishes. Callers therefore have no member-declaration-order rule and no
manual callback-drain sequence to remember.

The controller temporarily installs the test CA in `LocalMachine\Root` and
the origin leaf in `LocalMachine\My`, then removes both. Run it only in a
disposable test VM.

## Build and validate

```powershell
cmake -S examples\wfp\kernel\tls-inspection-proxy `
      -B artifacts\examples\wfp-kernel-tls -A x64 -DBUILD_TESTING=ON
cmake --build artifacts\examples\wfp-kernel-tls --config Debug
ctest --test-dir artifacts\examples\wfp-kernel-tls `
      -C Debug --output-on-failure
```

After test-signing/loading the driver, run the adjacent acceptance executable
from an elevated shell:

```powershell
.\crtsys_wfp_kernel_tls_inspection_proxy_acceptance.exe
```

It launches the sibling controller and validates real IPv4/IPv6 redirect
records, two TLS legs, SNI, HTTP/1.1 and HTTP/2 ALPN, permit/block,
request/response transforms, malformed and idle ClientHello failure, bounded
capture telemetry, policy removal, direct connectivity, and cleanup. Success
begins with:

```text
Kernel TLS inspection acceptance PASS:
```

The install-free CTest uses `driver/inspection_policy.cpp` directly to validate the
same ALPN, transform, block, HTML rewrite, and 32 KiB fail-closed policy
without loading a driver or changing system state.
