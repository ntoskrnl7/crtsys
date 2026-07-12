# NTL Wait Helpers

[Back to NTL docs](./README.md)

Header: [`include/ntl/wait`](../../include/ntl/wait)

`ntl::wait` helpers provide a small shared vocabulary for NTL wrappers that
already expose `wait(LARGE_INTEGER*)`, such as [`ntl::event`](./event.md),
[`ntl::timer`](./timer.md), and [`ntl::system_thread`](./system-thread.md).

These helpers do not hide the underlying WDK wait model. They only remove the
repeated timeout boilerplate and make timeout-vs-signal checks explicit.

## Basic Use

```cpp
ntl::event completed;

if (ntl::wait_timed_out(ntl::try_wait(completed))) {
  return STATUS_PENDING;
}

auto status = ntl::wait_for(completed, 1000);
if (ntl::wait_signaled(status)) {
  return STATUS_SUCCESS;
}

return status;
```

## API

- `ntl::zero_timeout()`
- `ntl::relative_timeout_ms(milliseconds)`
- `ntl::wait_signaled(status)`
- `ntl::wait_timed_out(status)`
- `ntl::try_wait(waitable)`
- `ntl::wait_for(waitable, milliseconds)`

`waitable` is any NTL object with `wait(LARGE_INTEGER*)`, including
`ntl::event`, `ntl::timer`, and `ntl::system_thread`.

## IRQL

The helper follows the waitable object's own IRQL contract. In NTL usage,
blocking waits are control-path operations and should be treated as
`PASSIVE_LEVEL` unless the exact WDK primitive and timeout mode have been
separately audited.

`try_wait()` uses a zero timeout and does not block, but it can still consume a
synchronization event/timer signal exactly like the underlying WDK wait call.

## Driver Test Coverage

The driver test covers:

- zero-timeout probe through `ntl::try_wait`
- signal and timeout status classification
- `ntl::wait_for` with `ntl::timer`
- `ntl::wait_for` with `ntl::system_thread`
