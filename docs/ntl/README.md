# NTL Documentation

[Back to README](../../README.md)

NTL is the optional C++ helper layer shipped with `crtsys`. It wraps common WDK
driver-control patterns with RAII-friendly C++ types while keeping the actual
kernel object model visible.

Use these pages when you want more detail than the compact
[NTL API reference](../ntl-api.md).

The reusable network protocol and transport headers use the
`<ntl/net/...>` include prefix and the `ntl::net` namespace. Protocol-specific
APIs use subnamespaces such as `ntl::net::http`, `ntl::net::http2`, and
`ntl::net::http3`. Windows-specific integration adapters retain their
independent top-level include prefixes and namespaces, such as `ntl::wfp`.

For buildable samples, see:

- [NTL typed IOCTL sample driver](../../examples/ntl-driver)
- [NTL RPC sample driver](../../examples/ntl-rpc-driver)
- [NTL kernel/user networking contract test](../../test/net/kernel-contracts)
- [NTL KMDF driver/app samples](../../examples/kmdf)
- [NTL KMDF DMA driver template](../../examples/kmdf/dma)
- [NTL KMDF USB driver template](../../examples/kmdf/usb)
- [NTL KMDF WMI driver/app sample](../../examples/kmdf/wmi)
- [NTL KMDF bus/PDO driver/app sample](../../examples/kmdf/bus)
- [NTL minifilter driver/app samples](../../examples/minifilter)
- [NTL WFP ALE connect-block driver/controller](../../examples/wfp/kernel/ale-connect-block)
- [NTL WFP connect-redirect coroutine TCP proxy](../../examples/wfp/user/connect-redirect)
- [NTL WFP TLS plaintext inspection proxy](../../examples/wfp/user/tls-inspection-proxy)
- [NTL WFP browser HTTPS inspection](../../examples/wfp/user/browser-https-inspection)
- [NTL WFP paired user/kernel samples](../../examples/wfp)
- [NTL kernel-direct TCP content filter](../../examples/wfp/kernel/tcp-content-filter)
- [NTL kernel-direct UDP content filter](../../examples/wfp/kernel/udp-content-filter)
- [NTL kernel-direct connect redirect](../../examples/wfp/kernel/connect-redirect)
- [NTL kernel-direct TLS inspection proxy](../../examples/wfp/kernel/tls-inspection-proxy)
- [NTL kernel-direct browser HTTPS inspection](../../examples/wfp/kernel/browser-https-inspection)
- [NTL kernel-direct HTTP/3 inspection](../../examples/wfp/kernel/http3-inspection)

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
| [KMDF engineering checklist](./kmdf-driver-checklist.md) | Ownership, callback lifetime, request cancellation, PnP/power, ABI, Driver Verifier, and software/hardware release gates |
| [Minifilter helpers](./minifilter.md) | `ntl::flt::main`, typed pre/post callbacks and contexts, per-volume metadata, owned legacy control devices, typed communication ports, and shared regions |
| [WFP guide for driver developers](./wfp-guide.md) | WFP concepts, kernel-centered and user-mode-centered execution models, typed callout decisions, payload boundaries, TLS/QUIC inspection, sample order, and verification |
| [WFP helpers](./wfp.md) | Typed callout layers and conditions, safe connect redirection and proxy handoff, fragmented packet/stream views, bounded coroutine observation, flow contexts, injection ownership, explicit session/persistent policy lifecycle, health checks, and event telemetry |
| [Network dual-runtime model](./network-dual-runtime.md) | One bounded protocol/policy contract across user and kernel code, explicit direct/offload capabilities, draining kernel execution, and the QUIC provider boundary |
| [Content inspection and framing](./inspection.md) | Complete UDP/TCP message boundaries, typed verdicts, custom framers, decoder adapters, bounded HTTP/3 inspection composition, and the TLS plaintext boundary |
| [HTTP, WebSocket, gRPC, and WebTransport inspection](./protocol-inspection.md) | Shared sync/async/stream transforms, HTTP/1, HTTP/2/HPACK, HTTP/3/QPACK, WebSocket, gRPC, WebTransport, content coding, ALPN, ECH, pinning, mTLS, and product enforcement boundaries |
| [User-mode Schannel TLS streams](./tls-stream.md) | Coroutine Schannel I/O, bounded ClientHello/SNI observation, injectable per-host certificate issuance/cache, TLS plaintext framing, HTTP/1 boundaries, and clean shutdown |
| [Device-control pattern](./device-control-pattern.md) | Practical `IOCTL` dispatch pattern using typed IOCTLs, remove locks, MDLs, and output reporting |
| [Typed IOCTL helper](./ioctl.md) | Compile-time `CTL_CODE` descriptors tied to request/reply payload types |
| [Device interface](./device-interface.md) | PnP `IoRegisterDeviceInterface` ownership and enable/disable helper |
| [RPC](./rpc.md) | Kernel/user RPC schemas, stable callback IDs, framing checks, and x86/x64 wire rules |
| [IPC shared memory](./ipc.md) | Transport-neutral region tokens and bounded shared-memory rings for IOCTL RPC and minifilter communication ports |
| [Synchronization](./synchronization.md) | `ntl::irql`, IRQL query/contract helpers, spin locks, ERESOURCE wrapper, and lock helpers |
| [Remove lock](./remove-lock.md) | `IO_REMOVE_LOCK` RAII guard for dispatch/remove/unload synchronization |
| [Event](./event.md) | `KEVENT` wrapper for notification/synchronization events |
| [Timer and DPC](./timer.md) | `KTIMER` and `KDPC` wrappers for one-shot timers, periodic timers, and DPC queueing |
| [System thread](./system-thread.md) | `PsCreateSystemThread` helper with `NTSTATUS` results and `ZwClose` handle ownership |
| [Wait helpers](./wait.md) | Common timeout and wait-status helpers for event, timer, and system-thread wrappers |
| [Work item](./work-item.md) | Deferring resident work to a `PASSIVE_LEVEL` system worker thread |
| [Passive executor](./passive-executor.md) | Inline-or-defer policy for running callables at `PASSIVE_LEVEL` |
| [Kernel coroutine context](./coroutine.md) | Optional C++20 awaiter for resuming an explicitly deferred continuation at `PASSIVE_LEVEL` |
| [User-mode coroutine sockets](./async-socket.md) | IOCP-backed `read_some_borrowed`, `read_exactly_borrowed`, and owning `write_all`, with cancellation and explicit task/buffer lifetimes |
| [Pool allocator](./pool-allocator.md) | Kernel pool-backed ownership helpers, STL allocators, PMR resources, pool tags, and IRQL rules |
| [Lookaside list](./lookaside-list.md) | Fixed-size kernel object cache wrapper over `LOOKASIDE_LIST_EX` |
| [MDL helper](./mdl.md) | RAII ownership for MDLs allocated by `IoAllocateMdl` |
| [I/O buffer mapping and minifilter swapping](./io-buffer-mapping.md) | IRP/minifilter input-output mappings and operation-neutral swapped buffers |
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
