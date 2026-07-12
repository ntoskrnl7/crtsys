# NTL Work Item Helper

[Back to NTL docs](./README.md)

`ntl::work_item` is a small C++ wrapper over WDK `WORK_QUEUE_ITEM` and
`ExQueueWorkItem`. It uses `ntl::event` internally to track completion.

Use it when driver code needs to defer a small unit of work to a system worker
thread that runs at `PASSIVE_LEVEL`.

If you want an inline-or-defer policy instead of direct work-item ownership,
use [`ntl::passive_executor`](./passive-executor.md).

Header: [`include/ntl/work_item`](../../include/ntl/work_item)

## Raw Context Form

```cpp
#include <ntl/work_item>

struct request_context {
  LONG completed = 0;
};

void do_passive_work(void* context) noexcept {
  auto* request = static_cast<request_context*>(context);

  // This callback runs on a system worker thread at PASSIVE_LEVEL.
  InterlockedExchange(&request->completed, 1);
}

request_context context;
ntl::work_item item(do_passive_work, &context);

auto status = item.queue();
if (status.is_ok()) {
  (void)item.wait();
}
```

## Callable Form

`ntl::passive_work_item<Callable>` stores the callable inline in the work-item
object. Construct it while the callable's storage and captures are valid, then
queue it when deferral is needed.

```cpp
volatile LONG completed = 0;

ntl::passive_work_item item([&] {
  InterlockedExchange(&completed, 1);
});

(void)item.queue();
(void)item.wait();
```

## API

`ntl::work_item`:

- `work_item(routine, context = nullptr, queue_type = DelayedWorkQueue)`
- `queue()`
- `wait()`
- `queued()`
- `last_status()`

`ntl::passive_work_item<Callable>`:

- `passive_work_item(callable, queue_type = DelayedWorkQueue)`
- `queue()`
- `wait()`
- `queued()`
- `last_status()`

## IRQL

- `queue()` may be called at `<= DISPATCH_LEVEL`.
- `wait()` is `PASSIVE_LEVEL` only.
- The worker callback runs at `PASSIVE_LEVEL`.
- Destroy a queued item only after `wait()` has completed, or from
  `PASSIVE_LEVEL` where the destructor can wait for completion.
- Do not let C++ exceptions escape the worker callback. Catch them inside the
  callback and translate them to driver-owned status/state if needed.

This helper queues the work. It does not make arbitrary captured state safe at
high IRQL. If a callable captures STL objects, strings, smart pointers, or
other runtime-backed state, construct and own that state from an audited context
and keep it alive until the work item has completed.
