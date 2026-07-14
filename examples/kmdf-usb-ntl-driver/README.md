# NTL KMDF USB Driver Template

This driver/app template demonstrates the NTL KMDF USB path:

1. create a `usb_device` in `EvtDevicePrepareHardware`;
2. select the first USB interface and enumerate bulk/interrupt pipes;
3. configure a continuous reader for the first input pipe;
4. start and stop that reader from D0 entry and exit;
5. expose descriptor, endpoint, and reader state to a user-mode app.

The INF deliberately uses the placeholder hardware ID
`USB\VID_FFFF&PID_FFFF`. Replace it with the real device ID before building or
installing the sample. Confirm that selecting configuration/interface zero and
continuously reading the first bulk or interrupt input endpoint match the
device protocol. Do not install this driver for an unrelated USB device.

USB target creation, descriptor access, and configuration selection happen at
`PASSIVE_LEVEL`. Continuous-reader completion normally runs at
`DISPATCH_LEVEL`; the sample therefore uses only a lock-free atomic counter in
that callback. Code added to the reader callback must support its actual IRQL.
Move general CRT/STL processing to a passive WDF work item or queue callback.

## CMake build

```powershell
cmake -S examples/kmdf-usb-ntl-driver `
      -B artifacts/examples/kmdf-usb-ntl-driver `
      -A x64
cmake --build artifacts/examples/kmdf-usb-ntl-driver --config Debug
```

## Visual Studio

Open `crtsys_kmdf_usb_ntl_sample_vs.sln`. Both projects consume the newest
installed `crtsys` NuGet package by default. Override `CrtSysPackageVersion`
when a fixed package version is required.

The template is build-testable without USB hardware. Runtime validation needs
a USB device matching the adapted INF and endpoint protocol.
