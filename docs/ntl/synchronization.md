# NTL Synchronization

[Back to NTL docs](./README.md)

This page covers NTL helpers that wrap IRQL, spin locks, and ERESOURCE.

## IRQL Helpers

Header: [`include/ntl/irql`](../../include/ntl/irql)

`ntl::irql` is an enum wrapper around common `KIRQL` values.

Helpers:

- `ntl::raised_irql`
  - RAII object that lowers IRQL in its destructor
- `ntl::raise_irql(KIRQL)`
- `ntl::raise_irql_to_dpc_level()`
- `ntl::raise_irql_to_synch_level()`
- `ntl::current_irql()`

Example:

```cpp
auto raised = ntl::raise_irql_to_dpc_level();
// Do a short audited DPC-level operation here.
```

IRQL: these helpers explicitly manipulate or observe IRQL. Keep raised scopes
as small as possible.

## Spin Lock

Header: [`include/ntl/spin_lock`](../../include/ntl/spin_lock)

`ntl::spin_lock` wraps `KSPIN_LOCK`.

API:

- `try_lock()`
- `lock()`
- `unlock()`
- `lock_at_dpc_level()`
- `unlock_from_dpc_level()`
- `test()`
- `native_handle()`

`ntl::unique_lock<ntl::spin_lock>` extends `std::unique_lock` with
`ntl::at_dpc_level_lock`.

Example:

```cpp
ntl::spin_lock lock;

{
  ntl::unique_lock guard(lock);
  // Resident, short, nonblocking work only.
}
```

DPC-level example:

```cpp
auto raised = ntl::raise_irql_to_dpc_level();
ntl::unique_lock guard(lock, ntl::at_dpc_level_lock);
```

IRQL: `lock()` and successful `try_lock()` raise to `DISPATCH_LEVEL`, and
`unlock()` restores the previous IRQL. `lock_at_dpc_level()` and
`unlock_from_dpc_level()` require the caller to already be running at
`DISPATCH_LEVEL`.

Code executed while holding the spin lock must be resident, short,
nonblocking, and must not allocate, wait, throw, or call arbitrary runtime/STL
helpers.

## ERESOURCE

Header: [`include/ntl/resource`](../../include/ntl/resource)

`ntl::resource` wraps `ERESOURCE`.

API:

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

Example:

```cpp
ntl::resource resource;

{
  ntl::shared_lock read_lock(resource);
  // Read shared state.
}

{
  ntl::unique_lock write_lock(resource);
  // Update shared state.
}
```

IRQL: `<= APC_LEVEL`, matching the blocking/resource-style synchronization
model. Do not use `ntl::resource` in DPC, ISR, or spin-lock-held paths.

`adopt_critical_region` is for callers that deliberately manage the critical
region boundary themselves.
