# NTL KMDF reference driver

[한국어 설명](./README.ko-KR.md)

This project is the recommended starting point for a production-oriented
software KMDF device. It combines the common rules that are deliberately
separated across the smaller teaching samples:

- a root-enumerated PnP device and device interface;
- `PrepareHardware`, release, D0 entry, and D0 exit state;
- device, queue, and per-file C++ contexts owned by WDF;
- a fixed-width, versioned user/kernel ABI;
- strict input and output size validation;
- a sequential passive queue and one-shot passive timer;
- the `mark cancelable` / `unmark cancelable` exactly-once race; and
- observable session, lifecycle, completion, cancellation, and IRQL values.

The driver snapshots a buffered operation before it pends the request.
`METHOD_BUFFERED` can alias input and output to the same system buffer, so the
query path also validates and copies its input header before clearing the
larger output structure.

The application opens two independent file sessions, validates the ABI and
PnP/power state, completes one delayed operation, cancels another, closes one
session, and verifies the final counters. Its success marker begins with
`NTL KMDF reference ok`.

This is a software-device reference, not a simulated hardware driver. Replace
the root hardware ID, interface GUID, IOCTL contract, and transform operation
for a real product. PCI, USB, DMA, interrupt, firmware, and class-extension
contracts still require their actual bus or device.

## Build

Open `crtsys_kmdf_reference_vs.sln`, or use CMake:

```powershell
cmake -S examples\kmdf\reference `
      -B artifacts\examples\kmdf-reference -A x64
cmake --build artifacts\examples\kmdf-reference --config Debug
```

## Disposable VM validation

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_reference_app.exe
.\remove.ps1
```

Use the repository's KMDF VM acceptance gate before treating a change as
ready. That gate repeats the application with x64 and WOW64 clients, restarts
the device, runs the selected driver under Driver Verifier, checks for new
bugchecks and dumps, removes the root device, and restores the prior verifier
configuration supplied to the gate.
