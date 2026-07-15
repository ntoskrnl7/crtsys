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
| [Handle and object ownership](./ntl/ownership.md) | `ntl::unique_handle`, `ntl::unique_object`, `try_reference_object_by_handle` |
| [File object facades](./ntl/file-object.md) | `ntl::file`, `ntl::kmdf::file`, and WDM/KMDF file-object ownership boundaries |
| [Registry](./ntl/registry.md) | `ntl::registry_key`, `ntl::registry_value`, `ntl::driver_config`, `try_open_driver_parameters`, typed registry value query/set helpers |
| [Driver, device, and IRP helpers](./ntl/driver-device-irp.md) | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::device_endpoint`, `try_create_device`, dispatch callbacks, `ntl::irp` |
| [KMDF helpers](./ntl/kmdf.md) | `ntl::kmdf::main`, C++ WDF contexts, typed manual-queue ownership and cancellation, hardware resources and power policy, common WDF lock/lookaside/collection/string/DPC objects, I/O/interrupt/timer/work-item/child-list facades, and registry/property helpers |
| [Typed IOCTL helper](./ntl/ioctl.md) | `ntl::ioctl`, `is_ioctl`, typed input/output buffer helpers |
| [Device interface](./ntl/device-interface.md) | `ntl::device_interface_link`, `try_register_device_interface` |
| [RPC](./ntl/rpc.md) | RPC schema macros, server lifetime, user-mode client calls |
| [Synchronization](./ntl/synchronization.md) | `ntl::irql`, IRQL contract helpers, spin locks, ERESOURCE wrapper |
| [Remove lock](./ntl/remove-lock.md) | `ntl::remove_lock`, `ntl::remove_lock_guard` |
| [Event](./ntl/event.md) | `KEVENT` wrapper for notification/synchronization events |
| [Timer and DPC](./ntl/timer.md) | `ntl::timer`, `ntl::kdpc`, one-shot timers, periodic timers, direct DPC queueing |
| [System thread](./ntl/system-thread.md) | `ntl::system_thread`, `PsCreateSystemThread`, `join`, native thread-handle ownership |
| [Wait helpers](./ntl/wait.md) | `ntl::zero_timeout`, `ntl::relative_timeout_ms`, `ntl::try_wait`, `ntl::wait_for` |
| [Work item](./ntl/work-item.md) | Deferring resident work to a `PASSIVE_LEVEL` system worker thread |
| [Passive executor](./ntl/passive-executor.md) | `ntl::passive_executor`, inline PASSIVE execution, detached nonpaged work posting |
| [Pool allocator](./ntl/pool-allocator.md) | Kernel pool-backed ownership helpers, STL allocators, PMR resources, pool tags, IRQL rules |
| [Lookaside list](./ntl/lookaside-list.md) | Fixed-size kernel object cache wrapper over `LOOKASIDE_LIST_EX` |
| [MDL helper](./ntl/mdl.md) | `ntl::mdl` ownership and mapping helpers |
| [Symbolic link](./ntl/symbolic-link.md) | RAII wrapper over `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` |
| [Unicode string](./ntl/unicode-string.md) | Adapting `std::wstring` storage to `UNICODE_STRING` |

For end-to-end driver/app snippets, see [NTL usage examples](./usage-examples.md).
