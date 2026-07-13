# NTL System Thread Helper

[Back to NTL docs](./README.md)

Header: [`include/ntl/system_thread`](../../include/ntl/system_thread)

`ntl::system_thread` owns the handle returned by `PsCreateSystemThread` and
closes that handle with `ZwClose`. It does not forcibly stop the thread.

For shared timeout helpers such as `ntl::wait_for(thread, milliseconds)`, see
[`ntl::wait`](./wait.md).

Use it when driver code needs a native kernel system thread but still wants
clear C++ ownership for the returned thread handle.

## Basic Use

```cpp
struct worker_context {
  std::atomic<long> value = 0;
};

void worker(void* context) {
  auto* state = static_cast<worker_context*>(context);
  state->value.store(42);
  PsTerminateSystemThread(STATUS_SUCCESS);
}

worker_context state;
auto thread = ntl::system_thread::create(worker, &state);
if (!thread) {
  return thread.status();
}

auto status = thread->join();
if (!status.is_ok()) {
  return status;
}
```

`join()` waits for the thread to terminate and closes the owned handle when the
wait succeeds. If the wait times out, the handle remains owned.

## API

- `ntl::system_thread`
  - `system_thread()`
  - `system_thread(HANDLE)`
  - move construction / move assignment
  - `create(routine, context, desired_access, object_attributes, process_handle,
    client_id)`
  - `get()`
  - `wait(timeout)`
  - `join(timeout)`
  - `close()`
  - `reset(handle = nullptr)`
  - `release()`
  - `valid()` / `operator bool()`

## Lifetime

Destroying `ntl::system_thread` closes the handle only. It does not wait for the
thread and does not terminate it. If the thread routine uses stack state,
objects, locks, events, STL containers, or other driver-owned resources, join
the thread before those resources are destroyed.

## IRQL

`PsCreateSystemThread`, `ZwWaitForSingleObject`, and `ZwClose` are control-path
operations. Use this helper from `PASSIVE_LEVEL`.

System thread routines run at `PASSIVE_LEVEL` in a system-thread context, but
the usual unload/lifetime rule still applies: the thread must not execute code
or touch data after the driver image that owns it is unloading.

## Driver Test Coverage

The driver test covers:

- `PsCreateSystemThread` creation through `ntl::system_thread::create`
- routine execution at `PASSIVE_LEVEL`
- `join()` waiting and handle close
- move ownership
- `release()` / adopt / `close()` ownership transfer
