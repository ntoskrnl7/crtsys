# NTL API Reference

[Back to README](../README.md)

NTL is the small C++ helper layer shipped with `crtsys`. It is intended for
kernel driver code that wants RAII-style ownership, callback registration, and
simple user-mode to kernel-mode RPC without hiding the underlying WDK objects.

Headers live under [`include/ntl`](../include/ntl).

## IRQL Contract Convention

NTL is designed mainly for driver control paths. If an API does not document a
broader contract, assume `PASSIVE_LEVEL`.

The API notes below use conservative contracts:

- `PASSIVE_LEVEL` means the helper may allocate, wait, throw, touch pageable
  code, or depend on runtime/STL state.
- `<= APC_LEVEL` means the helper may use WDK primitives that allow APC-level
  callers, but it is still not DPC/ISR-safe.
- `<= DISPATCH_LEVEL` or DPC-specific contracts are documented only for APIs
  that are intentionally written for that context.

See [Design Rationale and Operational Boundaries](./design-rationale.md) for
the project-level execution model.

## Entry Point

When `CRTSYS_NTL_MAIN` is enabled, implement:

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys` routes the WDK driver entry point into this function. The wrapper also
uses the stack expansion helper before calling into `ntl::main`.

IRQL: `PASSIVE_LEVEL`.

## `ntl::status`

Header: [`include/ntl/status`](../include/ntl/status)

`ntl::status` wraps `NTSTATUS`.

- `status(NTSTATUS status)`
- `bool is_ok() const`
- `bool is_info() const`
- `bool is_warn() const`
- `bool is_err() const`
- `operator NTSTATUS() const`
- `static status ok()`

IRQL: value-only operations do not allocate or wait. The surrounding WDK call
path still determines whether a given status flow is valid at the current IRQL.

## Exceptions

Header: [`include/ntl/except`](../include/ntl/except)

- `ntl::exception`
  - stores an `ntl::status`
  - carries an explanatory message
- `ntl::seh::try_except(fn, args...)`
  - runs a callable inside MSVC SEH using `__try` / `__except`
  - returns `{true, 0}` on success or `{false, exception_code}` from
    `GetExceptionCode()`

IRQL: treat C++ exception paths as `PASSIVE_LEVEL` only. Do not throw across
WDK callback boundaries, spin-lock-held regions, DPC, ISR, or paging I/O paths
unless that exact behavior is separately tested and documented.

## Stack Expansion

Header: [`include/ntl/expand_stack`](../include/ntl/expand_stack)

Use `ntl::expand_stack` around call paths that may need more stack than the
default kernel stack.

```cpp
auto opts = ntl::expand_stack_options().wait(true);
auto result = ntl::expand_stack(std::move(opts), [] {
  return do_expensive_work();
});
```

API:

- `ntl::expand_stack_size_max`
- `ntl::expand_stack_options`
  - `stack_size(size_t)`
  - `wait(bool)`
  - `ignore_failure(bool)`
- `ntl::expand_stack(options, fn, args...)`

IRQL: documented `crtsys` usage is `PASSIVE_LEVEL`. The wrapper uses C++
runtime paths and may throw on failure. Treat it as a control-path helper, not
as a hot-path escape hatch.

## Driver Object

Header: [`include/ntl/driver`](../include/ntl/driver)

`ntl::driver` wraps `DRIVER_OBJECT`.

- `create_device<Extension>(device_options&)`
  - creates an `ntl::device<Extension>`
  - constructs the extension object in the device extension area
- `on_unload(callback)`
  - registers a C++ unload callback
- `name() const`
  - returns the driver name as `std::wstring`

IRQL: `PASSIVE_LEVEL`. The helper uses C++ objects and containers and is
intended for driver initialization, unload registration, and setup paths.

## IRP View

Header: [`include/ntl/irp`](../include/ntl/irp)

`ntl::irp` is a non-owning view over the dispatch-time `PIRP`. It does not
complete, reference, or retain the IRP.

- `get() const`
  - returns the raw `PIRP`
- `operator->() const`
  - returns the raw `PIRP`
- `stack_location() const`
  - returns `IoGetCurrentIrpStackLocation()`
- `major_function() const`
  - returns the current major function
- `status() const` / `status(NTSTATUS)`
  - reads or writes `IoStatus.Status`
- `information() const` / `information(ULONG_PTR)`
  - reads or writes `IoStatus.Information`

IRQL: follows the dispatch routine that supplied the IRP.

## Device Object

Header: [`include/ntl/device`](../include/ntl/device)

`ntl::device_options` configures device creation.

- `name(std::wstring)`
- `type(DEVICE_TYPE)`
- `exclusive(bool = true)`
- `name() const`
- `type() const`
- `is_exclusive() const`

`ntl::device<Extension>` owns a `PDEVICE_OBJECT`.

- `extension()`
  - returns the typed extension object
- `on_create(callback)`
  - registers an `IRP_MJ_CREATE` handler
  - callback signature: `void(ntl::irp&)`
- `on_close(callback)`
  - registers an `IRP_MJ_CLOSE` handler
  - callback signature: `void(ntl::irp&)`
- `on_device_control(callback)`
  - registers an `IRP_MJ_DEVICE_CONTROL` handler
- `name() const`
- `type() const`
- `detach()`
  - releases ownership of the raw device object

Device control helper types:

- `ntl::device_control::code`
- `ntl::device_control::in_buffer`
- `ntl::device_control::out_buffer`
- `ntl::device_control::dispatch_fn`

IRQL: `PASSIVE_LEVEL` unless a specific dispatch path has been audited and
documented otherwise. The wrapper uses C++ callbacks and ownership helpers.

## RPC

Headers:

- [`include/ntl/rpc/common`](../include/ntl/rpc/common)
- [`include/ntl/rpc/server`](../include/ntl/rpc/server)
- [`include/ntl/rpc/client`](../include/ntl/rpc/client)

The RPC helpers generate matching server and client stubs around
`DeviceIoControl`. Serialization uses `zpp::serializer`.

Common macros:

- `NTL_RPC_BEGIN(name)`
- `NTL_RPC_END(name)`
- `NTL_ADD_CALLBACK_0`
- `NTL_ADD_CALLBACK_1`
- `NTL_ADD_CALLBACK_2`
- `NTL_ADD_CALLBACK_3`
- `NTL_ADD_CALLBACK_4`
- `NTL_ADD_CALLBACK_5`
- `NTL_ADD_CALLBACK_ID_0`
- `NTL_ADD_CALLBACK_ID_1`
- `NTL_ADD_CALLBACK_ID_2`
- `NTL_ADD_CALLBACK_ID_3`
- `NTL_ADD_CALLBACK_ID_4`
- `NTL_ADD_CALLBACK_ID_5`

`NTL_ADD_CALLBACK_N` uses `__LINE__` as the callback ID. This is convenient for
small internal or test schemas, but the schema line number becomes part of the
ABI. Use `NTL_ADD_CALLBACK_ID_N` when callback IDs need to remain stable across
schema formatting or reordering.

Types:

- `ntl::rpc::server`
  - kernel-mode holder for the RPC device
- `ntl::rpc::client`
  - user-mode client
  - `invoke<Ret>(index, args...)`

See [`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp) for a complete
shared RPC schema example. For a smaller app/driver walkthrough, see
[NTL usage examples](./usage-examples.md).

IRQL: server-side callbacks should be treated as `PASSIVE_LEVEL`. The
user-mode `ntl::rpc::client` side is outside kernel IRQL rules but still
depends on the driver-side dispatch contract.

## IRQL

Header: [`include/ntl/irql`](../include/ntl/irql)

`ntl::irql` is an enum wrapper around common `KIRQL` values.

Helpers:

- `ntl::raised_irql`
  - RAII object that lowers IRQL in its destructor
- `ntl::raise_irql(KIRQL)`
- `ntl::raise_irql_to_dpc_level()`
- `ntl::raise_irql_to_synch_level()`
- `ntl::current_irql()`

IRQL: these helpers explicitly manipulate or observe IRQL. Keep the RAII object
scoped as tightly as possible and do not call runtime-backed helpers while the
IRQL is elevated unless those helpers document that context.

## Spin Lock

Header: [`include/ntl/spin_lock`](../include/ntl/spin_lock)

`ntl::spin_lock` wraps `KSPIN_LOCK`.

- `try_lock()`
- `lock()`
- `unlock()`
- `lock_at_dpc_level()`
- `unlock_from_dpc_level()`
- `test()`
- `native_handle()`

`ntl::unique_lock<ntl::spin_lock>` extends `std::unique_lock` with
`ntl::at_dpc_level_lock`.

IRQL: `lock()` and successful `try_lock()` raise to `DISPATCH_LEVEL` and
`unlock()` restores the previous IRQL. `lock_at_dpc_level()` and
`unlock_from_dpc_level()` require the caller to already be running at
`DISPATCH_LEVEL`. Code executed while holding the spin lock must be resident,
short, nonblocking, and must not allocate, wait, throw, or call arbitrary
runtime/STL helpers.

## ERESOURCE

Header: [`include/ntl/resource`](../include/ntl/resource)

`ntl::resource` wraps `ERESOURCE`.

- `try_lock()`
- `try_lock_shared()`
- `lock()`
- `unlock()`
- `lock_shared()`
- `unlock_shared()`
- `locked() const`
- `locked_exclusive() const`
- `locked_shared() const`
- `lock_no_critical_region(bool wait = true)`
- `lock_shared_no_critical_region(bool wait = true)`
- `unlock_no_critical_region()`
- `unlock_shared_no_critical_region()`
- `convert_to_shared()`
- `waiter_count() const`
- `shared_waiter_count() const`
- `native_handle()`

Lock helpers:

- `ntl::unique_lock<ntl::resource>`
- `ntl::shared_lock<ntl::resource>`
- `ntl::adopt_critical_region`

IRQL: `<= APC_LEVEL`, matching the blocking/resource-style synchronization
model. Do not use `ntl::resource` in DPC, ISR, or spin-lock-held paths.
`adopt_critical_region` is for callers that deliberately manage the critical
region boundary themselves.

## Unicode String

Header: [`include/ntl/unicode_string`](../include/ntl/unicode_string)

`ntl::unicode_string` adapts `std::wstring` to `UNICODE_STRING`.

- construct from `std::wstring`
- `c_str() const`
- `operator*()`

IRQL: construction from `std::wstring` should be treated as `PASSIVE_LEVEL`.
Accessors are lightweight, but the lifetime and storage still belong to the
owning C++ object.
