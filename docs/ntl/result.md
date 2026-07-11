# NTL Result

[Back to NTL docs](./README.md)

Header: [`include/ntl/result`](../../include/ntl/result)

`ntl::result<T>` carries either a value or an `NTSTATUS`. It is useful for
driver helpers that should preserve kernel-style status propagation without
forcing every caller into exceptions or output parameters.

## API

- `ntl::result<T>`
  - `result()`
  - `result(T)`
  - `result(ntl::unexpected_status)`
  - `static success(args...)`
  - `static failure(NTSTATUS)`
  - `bool has_value() const`
  - `explicit operator bool() const`
  - `ntl::status status() const`
  - `value()`
  - `operator*`, `operator->`
  - `value_or(fallback)`
- `ntl::result<void>`
  - `static success(NTSTATUS = STATUS_SUCCESS)`
  - `static failure(NTSTATUS)`
  - `bool has_value() const`
  - `status()`
  - `value()`
- `ntl::unexpected(status)`
- `ntl::ok(value)`
- `ntl::ok()`

## Example

```cpp
ntl::result<ntl::pool_buffer> make_control_buffer(size_t bytes) {
  auto buffer = ntl::make_pool_buffer(bytes, ntl::pool_kind::nonpaged, "NTLb");
  if (!buffer) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  }

  return buffer;
}

auto buffer = make_control_buffer(4096);
if (!buffer) {
  return buffer.status();
}

use_buffer(buffer->get());
```

Use `value()` when the caller intentionally wants exception-style control flow:

```cpp
try {
  auto buffer = make_control_buffer(4096).value();
  use_buffer(buffer.get());
} catch (const ntl::exception& e) {
  return e.get_status();
}
```

## IRQL

Checking `has_value()`, `operator bool`, and `status()` is value-only and follows
the caller context. Constructing, moving, destroying, or accessing the contained
`T` follows `T`'s own rules. Calling `value()` on a failed result throws
`ntl::exception`, so treat that failed-value path as `PASSIVE_LEVEL`.

## Result-Producing Helpers

Existing NTL APIs keep their original behavior for source compatibility.
Result-returning variants are added where a helper creates a value and failure
is naturally represented by `NTSTATUS`:

- `ntl::try_make_pool_buffer(...) -> ntl::result<ntl::pool_buffer>`
- `ntl::try_make_pool<T>(...) -> ntl::result<ntl::pool_ptr<T>>`
- `ntl::driver::try_create_device<T>(...) -> ntl::result<std::shared_ptr<ntl::device<T>>>`
- `ntl::try_create_symbolic_link(...) -> ntl::result<ntl::symbolic_link>`

This keeps simple status-only operations as `ntl::status`, exception-oriented
constructors as constructors, and value-producing factory paths as
`ntl::result<T>`.
