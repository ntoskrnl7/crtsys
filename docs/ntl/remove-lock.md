# NTL Remove Lock

[Back to NTL docs](./README.md)

Header: [`include/ntl/remove_lock`](../../include/ntl/remove_lock)

`ntl::remove_lock` wraps `IO_REMOVE_LOCK`. Use it for dispatch paths that must
finish before a remove, unload, or teardown path releases the final reference.

## Example

```cpp
class device_state {
public:
  ntl::remove_lock remove_lock{"RMVl"};
};

device.on_device_control([](const ntl::device_control::code&,
                            const ntl::device_control::in_buffer&,
                            ntl::device_control::out_buffer&) {
  auto guard = state.remove_lock.acquire(current_irp);
  if (!guard) {
    complete_irp(guard.status());
    return;
  }

  // Work is protected until guard leaves scope.
});

driver.on_unload([&] {
  state.remove_lock.release_and_wait();
});
```

## API Summary

- `ntl::remove_lock(tag, max_locked_minutes, high_watermark)`
- `acquire(tag) -> ntl::result<ntl::remove_lock_guard>`
- `release(tag)`
- `release_and_wait(tag)`
- `native_handle()`
- `ntl::remove_lock_guard::reset()`

Prefer the guard returned from `acquire()` over calling `release()` manually in
normal dispatch code. `release_and_wait()` is for the teardown path after the
object stops accepting new work. After `release_and_wait()` succeeds, the
wrapper is terminal: later `acquire()` calls return `STATUS_DELETE_PENDING`
instead of touching the native remove-lock state again. The wrapper performs
the remove-path native acquire internally before calling
`IoReleaseRemoveLockAndWait`, so callers do not need to keep a separate guard
only to satisfy that WDK precondition.

## IRQL

Follow the WDK `IO_REMOVE_LOCK` contract. Do not call `release_and_wait()` from
a path that cannot wait.

## Driver Test Coverage

The driver test suite exercises:

- acquire success
- guard move and reset
- multiple acquired references
- `release_and_wait`
- post-remove acquire rejection
