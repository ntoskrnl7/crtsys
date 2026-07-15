# NTL KMDF DMA Driver Template

This buildable driver template demonstrates the complete NTL/KMDF packet-DMA
control flow:

1. create a PnP device, DMA enabler, common buffer, transaction, interrupt,
   and sequential write queue;
2. initialize a request-backed DMA transaction;
3. program a scatter/gather descriptor table from the program-DMA callback;
4. report transfer completion from the interrupt DPC;
5. release the transaction and complete the original request.

The prepare-hardware path iterates `ntl::kmdf::resource_list` and obtains the
device register range through `resource_descriptor::memory()`. This keeps the
raw `CM_PARTIAL_RESOURCE_DESCRIPTOR` union out of ordinary driver code while
retaining `native()` for uncommon resource types.

The template is intentionally not supplied with an INF. DMA register layout,
interrupt status bits, descriptor format, transfer direction, alignment, and
hardware ID are device-specific. Replace the contents of `sample_hardware`,
then add an INF matching the real PCI or SoC device. Do not install this driver
for an unrelated device.

The program-DMA callback and interrupt DPC run at `DISPATCH_LEVEL`. They use
only nonpageable state and WDF/WDK operations valid at that level. The example
does not use the PASSIVE_LEVEL CRT/STL surface from either callback. The ISR
runs at DIRQL and is even more restricted. When adapting the sample, every
NTL, WDF, WDK, CRT, or STL operation added to these callbacks must explicitly
support the callback's IRQL. Move general STL work to a passive queue callback
or a passive work item instead of assuming it is safe at an elevated IRQL.

The one standard-library object shared with the DPC is
`std::atomic<WDFREQUEST>`. The source enforces `is_always_lock_free` with a
`static_assert`, so the supported targets cannot silently substitute a runtime
lock implementation at `DISPATCH_LEVEL`.

## CMake build

```powershell
cmake -S examples/kmdf-dma-ntl-driver `
      -B artifacts/examples/kmdf-dma-ntl-driver `
      -A x64
cmake --build artifacts/examples/kmdf-dma-ntl-driver --config Debug
```

## Visual Studio

Open `crtsys_kmdf_dma_ntl_sample_vs.sln`. The project consumes the newest
installed `crtsys` NuGet package by default. Override `CrtSysPackageVersion`
when a fixed package version is required.

This sample can be compiled without DMA hardware. Runtime validation requires
a device whose resources and register contract match the adapted code.
