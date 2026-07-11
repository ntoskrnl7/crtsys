# NTL Event Helper

[Back to NTL docs](./README.md)

`ntl::event` is a small wrapper over WDK `KEVENT`.

Use it when NTL code needs a notification or synchronization event without
spelling out the `KeInitializeEvent` / `KeSetEvent` / `KeWaitForSingleObject`
sequence at each call site.

Header: [`include/ntl/event`](../../include/ntl/event)

## Example

```cpp
#include <ntl/event>

ntl::event completed;

completed.set();

if (completed.signaled()) {
  (void)completed.wait();
}
```

Synchronization event:

```cpp
ntl::event one_shot(ntl::event_type::synchronization);
one_shot.set();
(void)one_shot.wait(); // consumes the signal
```

## API

- `event(event_type type = event_type::notification,
         bool initial_state = false)`
- `native()`
- `set(increment = IO_NO_INCREMENT, wait = false)`
- `reset()`
- `clear()`
- `read_state()`
- `signaled()`
- `wait(timeout = nullptr)`
- `wait(wait_reason, wait_mode, alertable, timeout = nullptr)`

## IRQL

The helper follows the underlying WDK event APIs:

- `set`, `reset`, `clear`, and `read_state` follow the native `KEVENT`
  contract.
- A blocking `wait()` is a `PASSIVE_LEVEL` control-path operation in NTL
  documentation.
- Nonblocking timeout-zero waits may be valid in narrower WDK contexts, but
  those paths should be audited separately.
