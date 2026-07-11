# NTL Context and IRQL

[Back to NTL docs](./README.md)

NTL is designed mainly for driver control paths. If an API does not document a
broader contract, assume `PASSIVE_LEVEL`.

The NTL pages use conservative contracts:

- `PASSIVE_LEVEL` means the helper may allocate, wait, throw, touch pageable
  code, or depend on runtime/STL state.
- `<= APC_LEVEL` means the helper may use WDK primitives that allow APC-level
  callers, but it is still not DPC/ISR-safe.
- `<= DISPATCH_LEVEL` means the helper is intentionally written around a WDK
  primitive that can be used at dispatch level when its other preconditions are
  satisfied.

The driver test suite still runs the broad C++/CRT/STL matrix from
`PASSIVE_LEVEL`. Higher-IRQL usage should be treated as a separate design
decision for the exact operation in question.

## Practical Reading Rule

If an NTL helper owns C++ objects, stores callbacks, uses STL containers, can
throw, or may allocate, treat it as `PASSIVE_LEVEL`.

If an NTL helper is a thin wrapper over a WDK primitive, follow the primitive's
native contract and the extra notes on the topic page.

Examples:

- `ntl::driver::on_unload` stores a C++ callback and is an initialization /
  teardown helper: `PASSIVE_LEVEL`.
- `ntl::spin_lock::lock_at_dpc_level` maps to a spin-lock path and requires the
  caller to already be at `DISPATCH_LEVEL`.
- `ntl::allocate_pool(..., ntl::pool_kind::nonpaged, ...)` can follow WDK pool
  rules for raw allocation, but a `std::vector` that uses the allocator is still
  a C++ container and should be treated as `PASSIVE_LEVEL` unless audited.

See [Design Rationale and Operational Boundaries](../design-rationale.md) for
the project-level execution model.
