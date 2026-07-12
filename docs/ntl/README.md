# NTL Documentation

[Back to README](../../README.md)

NTL is the optional C++ helper layer shipped with `crtsys`. It wraps common WDK
driver-control patterns with RAII-friendly C++ types while keeping the actual
kernel object model visible.

Use these pages when you want more detail than the compact
[NTL API reference](../ntl-api.md).

## Topics

| Topic | Use it for |
| --- | --- |
| [Context and IRQL](./context.md) | Shared IRQL language and how to read NTL execution-context notes |
| [Status, exceptions, and stack expansion](./status-exceptions-stack.md) | `ntl::status`, `ntl::exception`, SEH boundary helper, and `ntl::expand_stack` |
| [Result](./result.md) | `NTSTATUS`-backed value-or-error helper for driver control paths |
| [Handle and object ownership](./ownership.md) | `ZwClose` handle ownership and `ObDereferenceObject` reference ownership |
| [Registry](./registry.md) | RAII wrapper for Zw registry keys, driver `Parameters` lookup, and typed value query/set helpers |
| [Driver, device, and IRP helpers](./driver-device-irp.md) | `ntl::main`, `ntl::driver`, `ntl::device`, dispatch callbacks, and `ntl::irp` |
| [RPC](./rpc.md) | Kernel/user RPC schema macros, server lifetime, client calls, and stable callback IDs |
| [Synchronization](./synchronization.md) | `ntl::irql`, IRQL query/contract helpers, spin locks, ERESOURCE wrapper, and lock helpers |
| [Event](./event.md) | `KEVENT` wrapper for notification/synchronization events |
| [Work item](./work-item.md) | Deferring resident work to a `PASSIVE_LEVEL` system worker thread |
| [Pool allocator](./pool-allocator.md) | Kernel pool-backed ownership helpers, STL allocators, PMR resources, pool tags, and IRQL rules |
| [Lookaside list](./lookaside-list.md) | Fixed-size kernel object cache wrapper over `LOOKASIDE_LIST_EX` |
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
