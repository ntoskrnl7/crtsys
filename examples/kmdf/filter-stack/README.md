# NTL KMDF filter-stack sample

[한국어 설명](./README.ko-KR.md)

This software-only sample installs a root-enumerated KMDF function driver and
an NTL KMDF upper filter in the same device stack.

The function driver publishes a device interface and handles a typed query
IOCTL. The filter calls `device_init::filter()`, formats the received request
using its current stack type, registers a typed completion callback, and sends
the request to `device::default_io_target()`. The target adds one to the input;
the filter completion adds ten and records its layer. The application requires
both transformations and both observable layer bits, so a target-only install
cannot produce a false pass.

Unrecognized PnP and power traffic remains framework-forwarded. The sample
does not use raw IRP preprocessing or claim a device-class protocol.

## Build

Open `crtsys_kmdf_filter_stack_sample_vs.sln`, or use CMake:

```powershell
cmake -S examples\kmdf\filter-stack `
      -B artifacts\examples\kmdf-filter-stack -A x64
cmake --build artifacts\examples\kmdf-filter-stack --config Debug
```

## Disposable VM smoke test

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_filter_stack_app.exe
.\remove.ps1
```

The expected line begins with `NTL KMDF filter stack ok` and reports
`layers=0x3`. Use only a disposable test VM when installing development
drivers.
