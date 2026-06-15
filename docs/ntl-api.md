# NTL API Reference

[Back to README](../README.md)

NTL is the small C++ helper layer shipped with `crtsys`. It is intended for
kernel driver code that wants RAII-style ownership, callback registration, and
simple user-mode to kernel-mode RPC without hiding the underlying WDK objects.

Headers live under [`include/ntl`](../include/ntl).

## Entry Point

When `CRTSYS_NTL_MAIN` is enabled, implement:

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys` routes the WDK driver entry point into this function. The wrapper also
uses the stack expansion helper before calling into `ntl::main`.

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

## Exceptions

Header: [`include/ntl/except`](../include/ntl/except)

- `ntl::exception`
  - stores an `ntl::status`
  - carries an explanatory message
- `ntl::seh::try_except(fn, args...)`
  - runs a callable inside SEH
  - returns `{success, exception_code}`

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

Types:

- `ntl::rpc::server`
  - kernel-mode holder for the RPC device
- `ntl::rpc::client`
  - user-mode client
  - `invoke<Ret>(index, args...)`

See [`test/common/rpc.hpp`](../test/common/rpc.hpp) for a complete shared RPC
schema example.

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

## Unicode String

Header: [`include/ntl/unicode_string`](../include/ntl/unicode_string)

`ntl::unicode_string` adapts `std::wstring` to `UNICODE_STRING`.

- construct from `std::wstring`
- `c_str() const`
- `operator*()`
