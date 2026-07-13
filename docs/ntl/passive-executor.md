# NTL Passive Executor

[Back to NTL docs](./README.md)

Header: [`include/ntl/passive_executor`](../../include/ntl/passive_executor)

`ntl::passive_executor` is the policy layer above
[`ntl::work_item`](./work-item.md). Use `ntl::work_item` when you want direct
ownership of one queued work item. Use `ntl::passive_executor` when the code
path wants to say: run this callable at `PASSIVE_LEVEL`, inline when that is
already true, otherwise defer it to a system worker thread.

## Basic Use

```cpp
ntl::passive_executor executor;

auto status = executor.execute([&] {
  // Runs inline if the caller is already at PASSIVE_LEVEL.
  // Otherwise it is copied/moved to nonpaged pool and posted as work item.
  do_passive_only_work();
});

if (!status.is_ok()) {
  return status;
}
```

## Caller-Owned Work

When the caller needs explicit completion status or wants to wait for the work,
create a caller-owned `ntl::passive_work_item` and let the executor queue it.

```cpp
auto item = executor.make_work_item([&] {
  do_passive_only_work();
});

auto status = executor.queue_and_wait(item);
if (!status.is_ok()) {
  return status;
}
```

## DPC To PASSIVE_LEVEL Handoff

DPC callbacks run at `DISPATCH_LEVEL`. Keep the DPC short: capture only resident
state, then post the runtime-heavy work to a passive executor.

```cpp
struct dpc_context {
  ntl::passive_executor* executor = nullptr;
  ntl::event completed;
  std::atomic<long> value = 0;
};

void on_dpc(void* context, void*, void*) noexcept {
  auto* state = static_cast<dpc_context*>(context);

  (void)state->executor->post([state] {
    // This runs later on a system worker thread at PASSIVE_LEVEL.
    state->value.store(42);
    state->completed.set();
  });
}
```

The executor and captured state must remain valid until the posted work has
completed. Driver unload paths should cancel/drain the source of DPCs and wait
for the posted passive work before freeing the state or unloading code.

## API

- `ntl::passive_executor(queue_type = DelayedWorkQueue, tag = default_pool_tag)`
- `queue_type()`
- `tag()`
- `execute(callable)`
- `post(callable)`
- `queue(passive_work_item&)`
- `queue_and_wait(passive_work_item&)`
- `make_work_item(callable)`

## IRQL

- `execute(callable)`:
  - runs inline at `PASSIVE_LEVEL`;
  - posts detached work at `<= DISPATCH_LEVEL` when not already passive;
  - returns `STATUS_INVALID_DEVICE_STATE` above `DISPATCH_LEVEL`.
- `post(callable)` may be called at `<= DISPATCH_LEVEL`.
- `queue_and_wait(item)` is `PASSIVE_LEVEL` only.
- worker callbacks run at `PASSIVE_LEVEL`.

Detached `post()` and non-passive `execute()` allocate callable storage from
nonpaged pool and free it after the worker callback returns. The callable and
everything it touches must still be valid when the worker runs. Driver unload
paths must still drain or otherwise prevent queued work from running after the
driver image is gone.

## Driver Test Coverage

The driver test covers:

- inline `execute()` at `PASSIVE_LEVEL`
- detached `post()` running on a worker thread at `PASSIVE_LEVEL`
- `execute()` from raised IRQL deferring to a worker thread
- DPC callback handoff through `post()`
- caller-owned `passive_work_item` through `queue_and_wait()`
