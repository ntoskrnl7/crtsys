# User HTTP/3 acceptance fixture

[한국어](./README.ko-KR.md)

This directory contains validation traffic, not product WFP code. Its C++
sources do not install services, call `DeviceIoControl`, use `Fwpm*`, or depend
on `ntl::wfp`. They launch the product service, act as the controlled MsQuic
client, generate protocol traffic, and decide whether the collected evidence
passes.

`acceptance_main.cpp` invokes:

```text
crtsys_wfp_http3_inspection_acceptance.exe
  <service.exe> <ipc-directory>
```

The fixture exercises both IPv4 and IPv6 for ordinary HTTP/3, dynamic QPACK,
gzip/deflate/Brotli, WebTransport, policy-generated 403 responses, policy
removal, and unavailable-callout fail-closed behavior. It verifies numeric
service evidence before printing these stable markers:

```text
controlled-msquic-http3: WebTransport PASS ...
controlled-msquic-http3: dynamic QPACK and codecs PASS
controlled-msquic-http3: WFP gate PASS ...
raw-msquic-loopback: ... malformed=replay-contract PASS
```

`replay_contract.cpp` is an install-free bounded framing/codec contract and is
registered with CTest. The live acceptance needs the product driver loaded in a
disposable VM, elevation, and an architecture-matching official `msquic.dll`.

The fixture can be built as part of the product example (recommended, so all
artifacts share one configuration output) or configured on its own:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
