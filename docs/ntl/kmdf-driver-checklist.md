# KMDF Driver Engineering Checklist

[Back to the KMDF API guide](./kmdf.md)

This checklist turns the `ntl::kmdf` object model into reviewable rules for a
real driver. It supplements, rather than replaces, the native WDF callback,
IRQL, synchronization, PnP, power, and cancellation contracts.

## Ownership map

| Value | Owner | Validity and transfer rule |
| --- | --- | --- |
| `driver`, `device`, `file`, `io_queue`, `io_target`, `memory`, `timer`, `work_item`, `interrupt`, `dma_*`, `wmi_*` | WDF | Non-owning facade. Do not retain it beyond the lifetime of its native WDF object. Parent long-lived children explicitly. |
| Queue-delivered `request` | Driver callback until one terminal action | Move-only right to complete, forward, requeue, or send. A successful transfer invalidates the source facade. |
| Request in a WDF manual queue | WDF | Retrieve it before treating it as driver-owned. Let the queue cancellation callback complete requests canceled while queued. |
| `found_request` | Driver-owned object reference, not request ownership | Destruction dereferences it. `try_retrieve()` attempts the atomic transition to a driver-owned `request`. |
| `driver_request` | Driver | Deletes an unsent driver-created request. Successful asynchronous send transfers ownership to its completion callback. |
| `registry_key` | Driver | Move-only; closes the WDF key on destruction. |
| `queried_interface<T>` | Driver | Move-only; calls `InterfaceDereference` exactly once. |
| `device_init` passed to `EvtDriverDeviceAdd` | WDF | Non-owning. Successful device creation consumes it; KMDF cleans it up if the callback returns first. |
| `control_device_init` or allocated `pdo_init` | Driver until device creation | Move-only owner. Destruction frees unconsumed initialization state. |
| C++ object context | WDF object | Constructed after context allocation and destroyed from the WDF destroy callback. Context constructors and destructors must be `noexcept`. |

Never store a borrowed request-buffer pointer, resource-list view, WMI buffer,
or callback argument after its documented WDF lifetime ends.

## Request state machine

```text
queue callback
    |
    +-- complete ------------------------------> done
    |
    +-- forward/requeue/send succeeds --------> WDF owns it
    |
    +-- retain outside WDF queue
            |
            +-- mark cancelable
                    |
                    +-- cancel callback -------> completes exactly once
                    |
                    +-- unmark succeeds -------> retaining path completes
                    |
                    +-- unmark = STATUS_CANCELLED
                                                cancel callback completes
```

- Do not mark a request cancelable while it remains in a WDF queue.
- After retrieving and retaining it, establish persistent state before calling
  `try_mark_cancelable()`, because cancellation may race immediately.
- Stop the producer of completion (timer, interrupt, target callback) before
  clearing retained state in the cancellation callback.
- `STATUS_CANCELLED` from `try_unmark_cancelable()` means the cancellation
  callback owns completion. Do not touch or complete the request again.
- If a move-qualified forward, requeue, or send fails, the driver still owns
  the original request and must choose another terminal action.

## Buffer and ABI rules

- Use fixed-width fields in user/kernel contracts and `static_assert` every
  cross-bitness layout.
- Put a size and version in product ABIs. Reject unsupported versions before
  reading later fields.
- Validate both WDF-reported buffer size and embedded contract size.
- With `METHOD_BUFFERED`, input and output can alias the same system buffer.
  Snapshot all input fields before zeroing or writing the output structure.
- Use `try_unsafe_user_*()` only in `EvtIoInCallerContext`, and lock or copy
  user memory before retaining it.
- Complete with the exact number of initialized output bytes.
- Keep IOCTL access bits least-privileged and align INF/device ACL policy with
  the threat model.

The buildable [reference driver](../../examples/kmdf/reference) demonstrates
these rules with a versioned ABI and both x64 and WOW64 clients.

## Callback and execution rules

- Callback templates require non-capturing `noexcept` functions with the exact
  signature. Persistent state belongs in a WDF context, not a lambda closure.
- Select `WdfExecutionLevelPassive` before using audited CRT/STL facilities.
- A passive timer must be one-shot. If automatic serialization is used with a
  passive parent, set the timer object's execution level to passive as well.
- Synchronous queue drain/purge, work-item flush, and `timer.stop(true)` are
  PASSIVE-level operations and must not wait on the currently executing
  callback.
- Interrupt ISR and synchronization callbacks obey the interrupt DIRQL
  contract. Move nontrivial work to the DPC or passive work item.
- Choose one synchronization owner for shared state: WDF object/queue
  serialization, an interrupt lock, a WDF spin/wait lock, or atomics with a
  documented protocol. Avoid accidental nesting.

## PnP and power lifecycle

```text
DeviceAdd
  -> PrepareHardware
  -> D0Entry
  -> I/O
  -> D0Exit
  -> ReleaseHardware
```

The sequence can repeat. Surprise removal, failed starts, rebalance, restart,
sleep, and partial initialization mean not every forward transition has a
matching successful predecessor.

- Acquire translated hardware resources in `PrepareHardware`; release only
  what was acquired in `ReleaseHardware`.
- Make D0 entry/exit idempotent around partial failures.
- Stop new I/O before tearing down state used by timers, interrupts, DMA, or
  lower-target completions.
- Do not invent resources for a software-enumerated device.
- A filter forwards requests it does not own and preserves lower-stack status
  and information unless its contract intentionally transforms them.
- A bus owns child identity and presence; the child function driver owns its
  function policy. Version driver-defined query interfaces like any other ABI.

## Shipping gate

A software-only change is ready only when all of the following pass:

1. x86 and x64 compile contracts build with `/W4 /WX`.
2. x64 driver packages pass INF/catalog/signability checks.
3. x64 and WOW64 applications validate returned state.
4. install, device restart, removal, cancellation, and repeated execution pass.
5. the selected binaries are visibly active in `verifier /query`.
6. Driver Verifier records real activity, such as loads and Special Pool
   allocations, without a verifier breakpoint or bugcheck.
7. no new dump or unexpected-reboot event appears after the Verifier boot.
8. test devices are absent and the prior Verifier configuration is restored.

DMA, USB, interrupt, PCI, firmware, wake, and class-extension drivers need an
additional hardware gate. A compile-only template is not runtime evidence.

The repository's [software-only runtime fixture](../../test/kmdf/runtime)
implements the repeatable VM portion of this gate. Keep the guest disposable
and attach a kernel debugger for verifier or stress work.
