# NTL API Reference

[Back to README](../README.md)

NTL is the small C++ helper layer shipped with `crtsys`. It is intended for
kernel driver code that wants RAII-style ownership, callback registration, pool
allocation helpers, and simple user-mode to kernel-mode RPC without hiding the
underlying WDK objects.

Headers live under [`include/ntl`](../include/ntl).

The detailed API reference is split by topic:

| Topic | Contents |
| --- | --- |
| [Context and IRQL](./ntl/context.md) | Shared IRQL language and execution-context rules |
| [Status, exceptions, and stack expansion](./ntl/status-exceptions-stack.md) | `ntl::status`, `ntl::exception`, SEH boundary helper, `ntl::expand_stack` |
| [Result](./ntl/result.md) | `ntl::result<T>`, `ntl::result<void>`, `ntl::unexpected`, `ntl::ok`, result-returning factory helpers |
| [Driver, device, and IRP helpers](./ntl/driver-device-irp.md) | `ntl::main`, `ntl::driver`, `ntl::device`, `try_create_device`, dispatch callbacks, `ntl::irp` |
| [RPC](./ntl/rpc.md) | RPC schema macros, server lifetime, user-mode client calls |
| [Synchronization](./ntl/synchronization.md) | `ntl::irql`, spin locks, ERESOURCE wrapper |
| [Event](./ntl/event.md) | `KEVENT` wrapper for notification/synchronization events |
| [Work item](./ntl/work-item.md) | Deferring resident work to a `PASSIVE_LEVEL` system worker thread |
| [Pool allocator](./ntl/pool-allocator.md) | Kernel pool-backed ownership helpers, STL allocators, PMR resources, pool tags, IRQL rules |
| [Lookaside list](./ntl/lookaside-list.md) | Fixed-size kernel object cache wrapper over `LOOKASIDE_LIST_EX` |
| [Symbolic link](./ntl/symbolic-link.md) | RAII wrapper over `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` |
| [Unicode string](./ntl/unicode-string.md) | Adapting `std::wstring` storage to `UNICODE_STRING` |

For end-to-end driver/app snippets, see [NTL usage examples](./usage-examples.md).
