# Kernel HTTP/3 acceptance fixture

[한국어](./README.ko-KR.md)

This directory owns the controlled client, generated traffic, assertions, and
final verdict for the kernel HTTP/3 example. It is deliberately outside
`examples/wfp/kernel/http3-inspection`: the product controller owns policy,
certificates, driver control, and lifecycle IPC, while this fixture owns only
validation behavior.

Its C++ sources do not use `ntl::wfp`, call `Fwpm*` or `DeviceIoControl`, or
install/control a Windows service. The entry point is:

```text
crtsys_wfp_kernel_http3_inspection_acceptance.exe
  <controller.exe> <ipc-directory>
```

The fixture drives IPv4/IPv6 HTTP/3 permit/block, dynamic QPACK, compressed
HTML, WebTransport streams/Datagrams/Capsules/reliable-reset, direct traffic
after policy removal, unavailable-callout isolation, restored traffic, capture
validation, and 96 sequential connections. It prints the stable final marker:

```text
Kernel HTTP/3 inspection PASS: IPv4/IPv6 WFP, ... capture, cleanup PASS
```

Configure it independently with:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
```

The recommended VM flow builds it through the product example so the driver,
controller, and fixture land in the same configuration directory. Live use
requires a disposable elevated VM with the compatible official `msquic.sys`
provider and sample driver loaded, plus an architecture-matching official
`msquic.dll` beside the fixture. Building this directory performs no install.
