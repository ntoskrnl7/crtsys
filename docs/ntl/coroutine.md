# NTL Kernel Coroutine Context

[Back to NTL docs](./README.md)

Header: [`include/ntl/coroutine`](../../include/ntl/coroutine)

`ntl::resume_on_passive()` is an optional C++20 awaiter for a kernel coroutine
that must continue at `PASSIVE_LEVEL`. It uses
[`ntl::passive_executor`](./passive-executor.md):

- at `PASSIVE_LEVEL`, the coroutine continues inline;
- through `DISPATCH_LEVEL`, it can suspend and resume on a system worker
  thread at `PASSIVE_LEVEL`;
- if the continuation cannot be queued, `co_await` returns the failed
  `ntl::status` in the original caller context.

```cpp
#include <ntl/coroutine>

kernel_task process_later() {
  const ntl::status resumed = co_await ntl::resume_on_passive();
  if (!resumed.is_ok())
    co_return;

  // This path runs at PASSIVE_LEVEL.
  use_passive_only_runtime();
}
```

Always check the returned status before doing passive-only work. A queue
failure cannot move the coroutine to another execution context, so the code
immediately after `co_await` first runs in the original context to observe that
failure.

An existing executor policy can be supplied explicitly:

```cpp
ntl::passive_executor executor{DelayedWorkQueue, "CORw"};
const ntl::status resumed = co_await ntl::resume_on_passive(executor);
if (!resumed.is_ok())
  co_return;
```

## Scope

This helper controls only the continuation that explicitly awaits
`resume_on_passive()`. It does not change the C++ coroutine model and does not
intercept an arbitrary `std::coroutine_handle::resume()`: a raw resume runs in
the context of the thread that calls it.

NTL does not impose a coroutine return type or own the coroutine frame. The
task owner must keep the frame, executor, captured state, and driver image alive
until the queued continuation has finished. Driver unload must stop new work
and drain every outstanding coroutine continuation before releasing that state
or unloading code.

## Diagnostics And Coverage

In Debug builds, the posted continuation reports a warning if it ever runs
above `PASSIVE_LEVEL`. This is a diagnostic for the NTL passive-resume path,
not a global coroutine hook.

The driver semantic test starts a coroutine at `DISPATCH_LEVEL`, awaits
`resume_on_passive()`, waits for completion, and verifies that the continuation
ran at `PASSIVE_LEVEL`.
