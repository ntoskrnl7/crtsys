# NTL Documentation

[Back to README](../../README.md)

NTL is the optional C++ helper layer shipped with `crtsys`. It wraps common WDK
driver-control patterns with RAII-friendly C++ types while keeping the actual
kernel object model visible.

Use these pages when you want more detail than the compact
[NTL API reference](../ntl-api.md).

For buildable samples, see:

- [NTL typed IOCTL sample driver](../../examples/ntl-driver)
- [NTL RPC sample driver](../../examples/ntl-rpc-driver)
- [NTL KMDF DMA driver template](../../examples/kmdf-dma-ntl-driver)
- [NTL KMDF USB driver template](../../examples/kmdf-usb-ntl-driver)
- [NTL KMDF WMI driver/app sample](../../examples/kmdf-wmi-ntl-driver)
- [NTL KMDF bus/PDO driver/app sample](../../examples/kmdf-bus-ntl-driver)

## Topics

| Topic | Use it for |
| --- | --- |
| [Context and IRQL](./context.md) | Shared IRQL language and how to read NTL execution-context notes |
| [Status, exceptions, and stack expansion](./status-exceptions-stack.md) | `ntl::status`, `ntl::exception`, SEH boundary helper, and `ntl::expand_stack` |
| [Result](./result.md) | `NTSTATUS`-backed value-or-error helper for driver control paths |
| [Handle and object ownership](./ownership.md) | Win32 `CloseHandle`, kernel `ZwClose`, and `ObDereferenceObject` ownership |
| [File object facades](./file-object.md) | Non-owning `PFILE_OBJECT` and `WDFFILEOBJECT` views and their ownership boundary |
| [Registry](./registry.md) | RAII wrapper for Zw registry keys, driver `Parameters` lookup, and typed value query/set helpers |
| [Driver, device, and IRP helpers](./driver-device-irp.md) | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::device_endpoint`, dispatch callbacks, and `ntl::irp` |
| [KMDF helpers](./kmdf.md) | Optional `ntl::kmdf::main`, C++ contexts, typed I/O, manual queues and cancellation, hardware resources, power policy, DMA/USB/WMI, common WDF objects, interrupt/timer/work-item, child-list/PDO, typed query interfaces, registry, and device-property facades |
| [Device-control pattern](./device-control-pattern.md) | Practical `IOCTL` dispatch pattern using typed IOCTLs, remove locks, MDLs, and output reporting |
| [Typed IOCTL helper](./ioctl.md) | Compile-time `CTL_CODE` descriptors tied to request/reply payload types |
| [Device interface](./device-interface.md) | PnP `IoRegisterDeviceInterface` ownership and enable/disable helper |
| [RPC](./rpc.md) | Kernel/user RPC schema macros, server lifetime, client calls, and stable callback IDs |
| [Synchronization](./synchronization.md) | `ntl::irql`, IRQL query/contract helpers, spin locks, ERESOURCE wrapper, and lock helpers |
| [Remove lock](./remove-lock.md) | `IO_REMOVE_LOCK` RAII guard for dispatch/remove/unload synchronization |
| [Event](./event.md) | `KEVENT` wrapper for notification/synchronization events |
| [Timer and DPC](./timer.md) | `KTIMER` and `KDPC` wrappers for one-shot timers, periodic timers, and DPC queueing |
| [System thread](./system-thread.md) | `PsCreateSystemThread` helper with `NTSTATUS` results and `ZwClose` handle ownership |
| [Wait helpers](./wait.md) | Common timeout and wait-status helpers for event, timer, and system-thread wrappers |
| [Work item](./work-item.md) | Deferring resident work to a `PASSIVE_LEVEL` system worker thread |
| [Passive executor](./passive-executor.md) | Inline-or-defer policy for running callables at `PASSIVE_LEVEL` |
| [Pool allocator](./pool-allocator.md) | Kernel pool-backed ownership helpers, STL allocators, PMR resources, pool tags, and IRQL rules |
| [Lookaside list](./lookaside-list.md) | Fixed-size kernel object cache wrapper over `LOOKASIDE_LIST_EX` |
| [MDL helper](./mdl.md) | RAII ownership for MDLs allocated by `IoAllocateMdl` |
| [Symbolic link](./symbolic-link.md) | RAII wrapper over `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` |
| [Unicode string](./unicode-string.md) | Adapting `std::wstring` storage to `UNICODE_STRING` |

## Context Rules

NTL APIs are designed mainly for driver initialization, unload, device control,
and other control paths. If a page does not explicitly document a wider
contract, assume `PASSIVE_LEVEL`.

When NTL exposes a lower-level WDK primitive, the primitive's native IRQL
contract still matters. For example, raw nonpaged pool allocation can follow
the WDK pool allocation rules, but using an STL container with that allocator
also brings in constructors, destructors, comparisons, exceptions, and other
runtime behavior. Treat container usage as `PASSIVE_LEVEL` unless the exact
operation and element type have been separately audited.
