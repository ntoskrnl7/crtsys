# NTL Status, Exceptions, and Stack Expansion

[Back to NTL docs](./README.md)

This page covers small runtime-facing helpers that are useful in driver control
paths: `ntl::status`, `ntl::exception`, `ntl::seh::try_except`, and
`ntl::expand_stack`.

## `ntl::status`

Header: [`include/ntl/status`](../../include/ntl/status)

`ntl::status` is a value-only wrapper around `NTSTATUS`.

API:

- `status(NTSTATUS status)`
- `bool is_ok() const`
- `bool is_info() const`
- `bool is_warn() const`
- `bool is_err() const`
- `operator NTSTATUS() const`
- `static status ok()`

Example:

```cpp
ntl::status create_control_device(ntl::driver& driver) {
  ntl::device_options options;
  options.name(L"\\Device\\demo").type(FILE_DEVICE_UNKNOWN);

  auto device = driver.create_device<void>(options);
  if (!device) {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  return ntl::status::ok();
}
```

IRQL: value-only operations do not allocate or wait. The surrounding WDK call
path still determines whether a given status flow is valid at the current IRQL.

## Exceptions

Header: [`include/ntl/except`](../../include/ntl/except)

Types and helpers:

- `ntl::exception`
  - stores an `ntl::status`
  - carries an explanatory message
- `ntl::seh::try_except(fn, args...)`
  - runs a callable inside MSVC SEH using `__try` / `__except`
  - returns `{true, 0}` on success or `{false, exception_code}` from
    `GetExceptionCode()`

Example:

```cpp
auto [ok, code] = ntl::seh::try_except([&] {
  probe_and_copy_user_buffer();
});

if (!ok) {
  return static_cast<NTSTATUS>(code);
}
```

Use `try_except` to keep an SEH boundary local. Do not use it as permission to
let arbitrary C++ exceptions or SEH exceptions leak across WDK callback
boundaries.

IRQL: treat C++ exception paths as `PASSIVE_LEVEL` only. Do not throw across WDK
callback boundaries, spin-lock-held regions, DPC, ISR, or paging I/O paths
unless that exact behavior is separately tested and documented.

## Stack Expansion

Header: [`include/ntl/expand_stack`](../../include/ntl/expand_stack)

Use `ntl::expand_stack` around call paths that may need more stack than the
default kernel stack.

API:

- `ntl::expand_stack_size_max`
- `ntl::expand_stack_options`
  - `stack_size(size_t)`
  - `wait(bool)`
  - `ignore_failure(bool)`
- `ntl::expand_stack(options, fn, args...)`

Example:

```cpp
auto options = ntl::expand_stack_options()
                   .stack_size(ntl::expand_stack_size_max)
                   .wait(true);

auto result = ntl::expand_stack(std::move(options), [] {
  return parse_large_control_payload();
});
```

`expand_stack` is useful for exception-heavy or STL-heavy control paths. It is
not a replacement for keeping hot paths small and predictable.

IRQL: documented `crtsys` usage is `PASSIVE_LEVEL`. The wrapper uses C++
runtime paths and may throw on failure.
