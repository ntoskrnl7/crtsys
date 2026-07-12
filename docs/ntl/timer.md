# NTL Timer and DPC Helpers

[Back to NTL docs](./README.md)

Header: [`include/ntl/timer`](../../include/ntl/timer)

`ntl::timer` wraps `KTIMER`, and `ntl::kdpc` wraps `KDPC`. They are thin WDK
helpers for drivers that need one-shot timers, periodic timers, or explicit DPC
queueing without hand-writing the native initialization ceremony every time.

These helpers intentionally keep the WDK model visible. A DPC callback still
runs in the DPC environment, so keep it resident, short, nonblocking, and free
of arbitrary STL/CRT work.

## One-Shot Timer

```cpp
#include <ntl/timer>

ntl::timer timer(ntl::timer_type::synchronization);

timer.set_once(ntl::relative_due_time_ms(100));

auto timeout = ntl::relative_due_time_ms(1000);
auto wait_status = timer.wait(&timeout);
if (!wait_status.is_ok()) {
  return wait_status;
}
```

`ntl::relative_due_time_ms(ms)` returns the negative 100ns-based `LARGE_INTEGER`
format used by `KeSetTimer` and `KeSetTimerEx` for relative due times.

## Timer With DPC

```cpp
struct timer_context {
  volatile LONG fired = 0;
};

void on_timer(void* context, void*, void*) noexcept {
  auto* state = static_cast<timer_context*>(context);
  InterlockedIncrement(&state->fired);
}

timer_context state;
ntl::kdpc callback(on_timer, &state);
ntl::timer timer;

timer.set_once(ntl::relative_due_time_ms(10), &callback);
```

The callback receives the context stored in the `ntl::kdpc` object plus the two
system arguments supplied by direct DPC queueing or the timer. The callback must
be `noexcept`.

## Periodic Timer

```cpp
ntl::timer timer;
timer.set_periodic(ntl::relative_due_time_ms(1000), 1000, &callback);

// Later, before releasing callback state:
timer.cancel();
```

`timer` cancels itself in its destructor. That prevents a still-queued timer
from later touching a destroyed `KTIMER`. It does not prove that a DPC already
queued by an expired timer has finished. If the DPC owns or touches external
state, the driver must synchronize with that callback before releasing the
state.

## Direct DPC Queueing

```cpp
ntl::kdpc callback(on_timer, &state);
callback.queue();
```

`kdpc::cancel()` maps to `KeRemoveQueueDpc` and returns whether a queued DPC was
removed before execution. `ntl::kdpc` also attempts this removal from its
destructor, but a callback that is already running still needs external
synchronization before its state is released.

## API

- `ntl::timer_type`
- `ntl::relative_due_time_ms(milliseconds)`
- `ntl::kdpc`
  - `kdpc(routine, context)`
  - `initialize(routine, context)`
  - `native()`
  - `queue(system_argument1, system_argument2)`
  - `cancel()`
- `ntl::timer`
  - `timer(timer_type)`
  - `native()`
  - `set_once(due_time, kdpc*)`
  - `set_periodic(due_time, period_ms, kdpc*)`
  - `cancel()`
  - `read_state()`
  - `signaled()`
  - `wait(...)`

## IRQL

- Timer setup/cancel follows the WDK `KeSetTimer*` / `KeCancelTimer` contract.
- Waiting on a timer is a blocking control-path operation in NTL usage; treat it
  as `PASSIVE_LEVEL`.
- DPC callbacks run at `DISPATCH_LEVEL`. Do not allocate paged memory, block,
  throw, call streams, or run arbitrary STL/CRT code from the callback.
- If DPC work needs STL/CRT, capture only the minimal state in the DPC and queue
  an [`ntl::work_item`](./work-item.md) or other PASSIVE_LEVEL path.

## Driver Test Coverage

The driver test covers:

- direct `ntl::kdpc::queue`
- DPC callback argument forwarding
- one-shot synchronization timer wait
- one-shot timer with DPC callback
- periodic timer setup and cancel
