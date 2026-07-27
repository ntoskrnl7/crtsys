# WDK KMDF Sample Coverage

This document maps the reusable KMDF mechanisms demonstrated by
`Windows-driver-samples` to NTL APIs and repository verification.

Coverage does not mean that NTL copies a Microsoft sample or renames every
`Wdf*` routine. A mechanism is covered when ordinary driver code can express
the lifetime and callback path through a typed public API, the contract is
compiled with the supported toolchains and architectures, the behavior is
documented, and a loaded-driver test makes runtime-sensitive behavior
observable where repeatable hardware is available.

Device-class protocols and class-extension APIs remain separate from the
common KMDF object model. A driver can use those native WDK contracts beside
`ntl::kmdf` through the explicit `native()`, `native_handle()`,
`native_object()`, and `wdm_*()` interoperation points.

## Coverage matrix

| Microsoft sample family | Reusable mechanisms | NTL coverage | Primary evidence |
| --- | --- | --- | --- |
| `general/ioctl/kmdf` | control/PnP devices, buffered IOCTLs, queues, file objects, per-handle state | Covered | `examples/kmdf/basic`, `examples/kmdf/reference`, and the software-only runtime suite |
| `general/echo/kmdf` | queue synchronization, delayed completion, cancellation, timer lifetime | Covered | `examples/kmdf/echo` and its success/cancel/restart runtime checks |
| Toaster function `simple` / `featured` | PnP function device, hardware and D0 callbacks, interfaces, idle policy | Covered | `examples/kmdf/pnp`, `examples/kmdf/wmi`, and VM restart/remove checks |
| Toaster bus `dynamic` / `static` | child lists, dynamic and static PDO concepts, resource requirements, eject and missing transitions | Covered for the reusable bus lifecycle | `examples/kmdf/bus` and its bus/function/app runtime path |
| Toaster filter `generic` / `sideband` / `toastmon` | filter FDO, forwarding, completion, lower-target lifetime, sideband control patterns | Covered for the common filter-stack path | `examples/kmdf/filter-stack` and its target/filter/app runtime path |
| `general/pcidrv/kmdf` | translated resources, interrupts, DPCs, DMA, power, registry, WMI | Covered for common KMDF mechanisms; hardware protocol is compile-only | `examples/kmdf/pnp`, `dma`, and `wmi` plus package compile contracts |
| `general/PLX9x5x` | packet DMA, common buffers, scatter/gather, interrupt completion | Covered as a hardware template | `examples/kmdf/dma`; runtime requires matching hardware |
| `usb/kmdf_fx2`, `usb/kmdf_enumswitches`, `usb/wdf_osrfx2_lab` | USB target/config/interface/pipe, continuous reader, interrupt and child-device composition | Covered for reusable USB/KMDF mechanisms | `examples/kmdf/usb`, `examples/kmdf/bus`, and package compile contracts; endpoint runtime requires matching hardware |
| `wmi/wmisamp` | MOF data blocks, query/set/item/method callbacks and events | Covered | `examples/kmdf/wmi` and its ROOT\\WMI application verifier |
| `serial/serial` | queue, request, interrupt, DPC, timer and target mechanics | Covered for common KMDF mechanics | common examples and compile contracts; UART register/protocol code remains native |
| PoFx WDF samples | component power and framework PoFx integration | Native interoperation | uncommon component-power policy remains outside the typed common surface |
| ACX, NetAdapterCx, WiFiCx, GPIO/SpbCx, UCM, HID and other class families | class-extension and device-protocol contracts layered on WDF | Out of common-surface scope | use the native class contract beside `ntl::kmdf`; add a focused adapter only when a real driver needs one |
| raw IRP preprocessing and miniport integration | WDM stack locations, port-driver or miniport ownership | Native interoperation | documented `KMDF Surface Boundary` and explicit WDM/native escape hatches |

## Cross-cutting verification gates

The following gates apply where relevant to a row:

1. Public examples and compile contracts build for x86 and x64 with `/W4 /WX`.
2. Callback signatures and request/object ownership transitions are checked at
   compile time; invalid copy or callback substitutions remain ill-formed.
3. Software-only control, PnP, bus, filter, WMI, cancellation, restart, and
   unload paths run in a disposable Windows VM.
4. Fixed-layout user/kernel contracts are architecture-neutral and are
   compiled for both client architectures.
5. Pending state is exercised through cancellation, target removal, device
   restart, or unload rather than validated only by a successful request.
6. Runtime applications make observable assertions. Debug output alone is not
   treated as verification.
7. Driver Verifier stress covers concurrent requests, cancellation, timers,
   work items, WDF object lifetime, and repeated unload.
8. Hardware-only paths must compile and document their resource, IRQL, and
   ownership contracts. They are not reported as runtime-covered without a
   matching device and protocol.

The host-side acceptance gate selects all software-only driver binaries in one
Verifier boot, runs each x64 package with x64 and WOW64 applications, exercises
device restart and repeated stress-driver load/unload, checks crash/dump and
device-cleanup state, and then restores the explicitly supplied prior Verifier
configuration.

## Definition of the common KMDF surface

The common surface includes driver/device entry, control and PnP devices,
typed contexts and callbacks, queues and forward progress, requests and I/O
targets, file objects, PnP/power/resource callbacks, interrupts, timers, work
items and DPCs, child lists and PDOs, query interfaces, registry/properties,
DMA, USB, WMI, and framework object synchronization.

The surface is sufficient when a normal control, function, filter, or bus
driver can keep its framework lifetime and ordinary I/O paths in
`ntl::kmdf`, while device-specific register layouts, protocol structures, and
class-extension calls remain recognizable native WDK code. Zero native calls
is not a coverage goal.

Update this matrix only when the corresponding public API, example, compile
contract, or runtime evidence changes. Keep VM build numbers, verifier logs,
and hardware-specific results in the fixture documentation rather than this
repository-wide summary.
