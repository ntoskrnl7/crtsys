# Kernel HTTP/3 inspection

[한국어](./README.ko-KR.md) · [WFP samples](../../README.md)

This example runs a real QUIC/TLS 1.3 and HTTP/3 endpoint inside the driver by
binding the official MsQuic kernel NMR provider. It is not a parser replay.

```text
controlled client UDP connect
  -> typed ALE_AUTH_CONNECT_V4/V6 WFP callout
  -> msquic.sys NMR provider
  -> kernel TLS 1.3 and QUIC streams
  -> HTTP/3 SETTINGS, frames, and bounded QPACK
  -> X-NTL-Block: 1 policy
  -> 200 or 403 HTML response
```

The product and acceptance roles are separate:

- `crtsys_wfp_kernel_http3_inspection.sys` owns the callouts, kernel MsQuic
  endpoint, HTTP/3/QPACK/codec processing, WebTransport state, bounded
  connection quota, telemetry, and PASSIVE_LEVEL reaper.
- `crtsys_wfp_kernel_http3_inspection_controller.exe` owns temporary
  certificates, dynamic WFP policy, driver control, and lifecycle IPC. It does
  not contain the controlled HTTP/3 client or print a `PASS` verdict.
- The MsQuic client, generated traffic, assertions, and final marker live under
  `test/wfp/runtime/fixtures/kernel/http3-inspection`.

The driver covers IPv4 and IPv6, permit/block, dynamic-QPACK blocked-stream
resume and acknowledgement, gzip/deflate/Brotli HTML, and WebTransport Extended
CONNECT, bidirectional/unidirectional streams, Datagrams, fragmented Capsules,
and reliable reset. `X-NTL-Block: 1` rejects Extended CONNECT with a final 403
before session activation. A separate policy with deliberately unregistered
callouts proves `callout_unavailable::block` without reaching the origin.

The 64-connection limit is concurrent, not lifetime total. Acceptance opens 96
additional sequential connections; shutdown callbacks and stream drain must
complete before the reaper releases each slot. Final telemetry requires zero
active connections and all closed slots reclaimed.

## Build outputs

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

One build produces these targets in the same configuration directory:

- `crtsys_wfp_kernel_http3_inspection`
- `crtsys_wfp_kernel_http3_inspection_controller`
- `crtsys_wfp_kernel_http3_inspection_acceptance`
- `crtsys_wfp_kernel_http3_inspection_policy_contracts`

The policy contract is install-free: it does not load MsQuic, install a driver,
change WFP state, or modify a certificate store. Real acceptance requires a
disposable VM with a compatible official `msquic.sys` provider, the sample
driver, and an architecture-matching official `msquic.dll` beside the
acceptance executable. Building never installs either driver on the host.

## Product controller interface

```text
crtsys_wfp_kernel_http3_inspection_controller.exe
  <controlled-application.exe> <ipc-directory>
```

The fixture supplies the controlled executable and lifecycle commands. See the
[kernel HTTP/3 fixture README](../../../../test/wfp/runtime/fixtures/kernel/http3-inspection/README.md).

This is a controlled kernel endpoint, not transparent NAT or arbitrary-browser
MITM for remote UDP/443. Browser certificate policy, ECH, pinning, mTLS, and
transparent bidirectional UDP routing remain explicit product boundaries.
