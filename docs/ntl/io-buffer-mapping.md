# NTL I/O Buffer Mapping and Minifilter Swapping

[Back to NTL docs](./README.md)

Headers:

- [`include/ntl/ipc/mapped_buffer`](../../include/ntl/ipc/mapped_buffer)
- [`include/ntl/ipc/io_buffer`](../../include/ntl/ipc/io_buffer)
- [`include/ntl/flt/io_buffer`](../../include/ntl/flt/io_buffer)
- [`include/ntl/flt/swapped_io_buffer`](../../include/ntl/flt/swapped_io_buffer)
- [`include/ntl/flt/pending_io`](../../include/ntl/flt/pending_io)
- [`include/ntl/flt/deferred_io`](../../include/ntl/flt/deferred_io)

The API separates two operations:

- **mapped** means that a buffer is visible in one connected user process;
- **swapped** means that a minifilter installed page-isolated replacement
  storage in the operation's IOPB.

Mapping, swapping, Filter Manager communication, and pending are intentionally
separate. There is no combined `pend_and_send` operation and callers never
select `from_direct_io`, `from_buffered_io`, or `from_neither_io` helpers.

## Process connection and wire identity

For a WDM file/IOCTL connection, capture the intended service process in its
create callback:

```cpp
auto connection = ntl::ipc::process_connection::try_capture_current_process({
    .max_mappings = 256,
    .max_mapped_bytes = 512u * 1024u * 1024u,
});
if (!connection)
  return connection.status();
```

Capture, map, and unmap are `PASSIVE_LEVEL` operations. Each mapping retains a
process reference. The connection assigns a monotonic generation and a mapping
ID, rejects new mappings after close begins, and synchronously runs mapping,
copy-back, and owned-resource rundown. If the target is already terminating,
rundown waits for its address space to be destroyed before releasing backing
pages. Disconnect also waits for a mapping attempt that has created a user VAD
but has not yet reached registry insertion.

Send `connection->generation()` in the session handshake. The wire descriptor
uses fixed-width fields and is stable across x86/x64:

```cpp
struct mapped_buffer_descriptor {
  std::uint64_t mapping_id;
  std::uint64_t generation;
  std::uint64_t mapped_address;
  std::uint64_t length;
  std::uint32_t access;
  std::uint32_t reserved;
};
```

The driver must authenticate which process is allowed to establish the
connection. A PID supplied later by an untrusted message is not a connection.
Call `connection->close()` on port/file disconnect, service exit, instance
teardown, and unload.

For an NTL Filter Manager communication port, this is automatic. The port
captures and references the connecting process before accepting the
connection; `communication_connection::mapping_process()` returns a copyable
handle to that shared mapping registry. Configure its independent VAD quota on
the port:

```cpp
ntl::ipc::process_connection_limits mapping_limits{
    .max_mappings = 32,
    .max_mapped_bytes = 64u * 1024u * 1024u,
};
ntl::flt::communication_port_options port_options;
port_options.process_mapping_limits(mapping_limits);

server.on_connect([](ntl::flt::communication_connection &client) {
  auto process = client.mapping_process();
  return process.accepts_mappings() ? STATUS_SUCCESS
                                    : STATUS_INVALID_DEVICE_STATE;
});
```

Filter Manager disconnect first blocks new port operations, then NTL closes
all mappings in that process and only afterward invokes the application's
disconnect observer. A copied connection facade therefore cannot resurrect a
mapping after disconnect, and callers do not look up a later PID.

## IRP input and output

```cpp
auto mapped = ntl::ipc::try_map_io_buffers(
    device_object, irp, connection.value());
if (!mapped)
  return mapped.status();

if (const auto *input = mapped->input())
  send_descriptor(input->descriptor());
if (const auto *output = mapped->output())
  send_descriptor(output->descriptor());
if (const auto *control = mapped->control_input())
  send_control_descriptor(control->descriptor());
```

NTL reads the major function, device flags, control code, and current stack
location. The automatic IRP adapter supports:

| Operation | Logical buffers |
| --- | --- |
| `IRP_MJ_WRITE` | `input()` |
| `IRP_MJ_READ` | `output()` |
| buffered IOCTL/FSCTL | logical `input()` and `output()` over one backing mapping |
| `METHOD_IN_DIRECT` | system header as `control_input()`, direct payload as `input()` |
| `METHOD_OUT_DIRECT` | system header as `control_input()`, direct payload as `output()` |
| neither IOCTL/FSCTL | independent `input()` and `output()` |

Only `IRP_MN_USER_FS_REQUEST` and `IRP_MN_KERNEL_CALL` use the FSCTL buffer
layout. Mount, verify, and load-file-system requests return
`STATUS_NOT_SUPPORTED`.

The entry point, rather than a mutable option bit, records whether the IRP is
being observed before or after completion:

```cpp
auto submitted = ntl::ipc::try_map_io_buffers(
    device_object, irp, connection);

auto completed = ntl::ipc::try_map_completed_io_buffers(
    device_object, irp, connection);
```

Use the second form for a completed read/output query. Its output is clamped to
`IoStatus.Information`; `METHOD_IN_DIRECT` remains input and is not clamped.
Warning completions can contain valid output and are not treated as hard
failures. `map_io_options` only controls access and isolation policy, so a
caller cannot accidentally turn a pre-completion call into a completed one by
setting a boolean.

### Page-safe default and zero-copy

A user-mode MDL mapping creates a page-granular VAD. A logical subrange cannot
prevent the service from addressing the rest of its first and last pages.
Consequently the high-level adapters use this policy by default:

```cpp
ntl::ipc::map_io_options options{
    .user_mdls = ntl::ipc::user_mdl_policy::isolate_partial_pages,
};
```

- A user MDL that exactly covers complete pages is mapped zero-copy.
- A partial-page MDL, a completed subrange shorter than its MDL, a kernel
  requestor buffer, and a nonpaged-pool MDL use fully zeroed isolated staging.
- Buffered kernel storage always uses staging unless the caller selects the
  explicit reject policy.
- For buffered control requests, staging copies only the initialized input
  prefix before completion. Any larger output capacity remains zero-filled,
  and copy-back is limited to the writable logical input/output ranges.

A driver whose service is trusted to see adjacent application bytes may opt
into the old zero-copy behavior:

```cpp
options.user_mdls =
    ntl::ipc::user_mdl_policy::allow_page_padding_exposure;
```

That opt-in is a security decision, not a performance hint. The low-level
`try_map_mdl(borrowed_mdl_view, ...)` API has the same caller-responsibility
contract. It records that NTL does not own the MDL; it does not certify that
the MDL is safe to disclose.

`max_staging_bytes`, the connection mapping count, and mapped-byte quota bound
memory retained by a client. The mapped-byte quota charges the complete
page-rounded VAD span, including an MDL byte offset, rather than only the
logical descriptor length. Staging and swapped-replacement limits also charge
the page-rounded physical allocations. `kernel_buffer_policy::reject` can be
used where copy fallback is not acceptable.

## Minifilter mapping

The callback phase is part of the type because `PFLT_CALLBACK_DATA` alone does
not say whether output is valid:

```cpp
auto write_input = ntl::flt::try_map_io_buffers(
    ntl::flt::as_pre(write_data), connection);

auto read_output = ntl::flt::try_map_io_buffers(
    ntl::flt::as_post(read_data), connection);
```

Adapters cover create EA, read/write, query/set file information, query/set EA,
query/set volume information, query/set security, query/set quota, directory
query/notification (including extended notification), FSCTL, and IOCTL.
Post-output length is bounded by `IoStatus.Information`, including warning
statuses such as `STATUS_BUFFER_OVERFLOW`. Where necessary, NTL calls
`FltLockUserBuffer`; raw user pointers are copied in the Filter Manager
requestor process.

The mapping helpers require `PASSIVE_LEVEL`. If a pre/post callback is not
already safe, queue it first:

```cpp
auto status = ntl::flt::queue_post_operation_at_passive(
    data.native(), &finish_read_at_passive, completion_context);
if (status.is_ok())
  return ntl::flt::post_result::more_processing_required;
```

The deferred routine either completes the pended operation by returning a
normal status, or returns `STATUS_PENDING` only after transferring it to a
`pending_pre_operation_queue`/`pending_post_operation_registry`.
If post deferral fails, fail the transformation, adopt the completion context,
run cleanup-only `complete()` (which is DISPATCH-safe after transfer), and
return finished processing. Do not attempt user mapping or raw-buffer copy-back
on that fallback path.

## Operation-neutral minifilter swapping

`swapped_io_buffers` exposes `input()`, `output()`, and `control_input()` while
selecting the correct IOPB fields internally. It is not limited to read/write.
It supports the minifilter operations listed above plus buffered, direct, and
neither FSCTL/IOCTL layouts. Unsupported minors and Fast I/O return
`STATUS_NOT_SUPPORTED`; a pre callback can disallow Fast I/O and retry on the
IRP path.

The operation type also controls whether a buffer selector is legal:

| Typed pre-operation | API form |
| --- | --- |
| CREATE EA, WRITE, SET_INFORMATION, SET_EA, SET_VOLUME_INFORMATION, SET_SECURITY, SET_QUOTA | `try_swap_io_buffers(operation, options)`; input is inferred |
| READ, QUERY_INFORMATION, QUERY_VOLUME_INFORMATION, DIRECTORY_CONTROL, QUERY_SECURITY | `try_swap_io_buffers(operation, options)`; output is inferred |
| QUERY_EA, QUERY_QUOTA, FILE_SYSTEM_CONTROL, DEVICE_CONTROL, INTERNAL_DEVICE_CONTROL | `try_swap_io_buffers(operation, swap_buffer::input/output/all, options)` |

`swap_io_options` contains only allocation and execution policies. It cannot
select input or output. Supplying a selector to READ/WRITE, or omitting one for
a dual-capable operation, is rejected at compile time:

```cpp
auto read = ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(read_data));

auto query_ea = ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(query_ea_data),
    ntl::flt::swap_buffer::output);
```

For control operations, `swap_buffer::input` includes the control header
exposed as `control_input()`. The actual transfer method still comes from the
IOPB control code; callers never select buffered, direct, or neither I/O.
Because `METHOD_BUFFERED` uses one backing buffer for both directions, NTL may
retain an internal output slot and expose `output()` so lower-driver output can
be copied back even when input transformation was selected. For a direct
method whose runtime layout has no selected direction, the call returns
`STATUS_NOT_SUPPORTED`.

Each replacement uses isolated pages. When an operation has an MDL field, NTL
derives a distinct partial replacement MDL from the locked backing MDL. The
page buffer's source MDL remains NTL-owned; Filter Manager owns/frees only the
installed partial MDL after post-operation. Filter Manager presents the
original IOPB parameters to post callbacks, so completion-context cleanup does
not rewrite or dirty those fields a second time. Noncached read/write
replacement capacity is rounded to the volume sector size, while the public
view and copy-back remain limited to the original logical length.

A user mapping must be closed before `apply()`, output copy-back, or completion.
`release_completion_context()` is the supported transfer after `apply()`; it
both moves the owner to the post callback and marks replacement MDLs as handed
to Filter Manager.

### Pre-write encryption

The replacement keeps the application's original write buffer unchanged:

```cpp
auto swapped =
    ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(write_data));
if (!swapped)
  return ntl::flt::pre_result::complete;

auto mapped = swapped->input()->try_map(
    crypto_connection, ntl::ipc::map_access::read_write);
if (!mapped)
  return ntl::flt::pre_result::complete;

// Send mapped->descriptor() to the service and wait/pend for encryption.
auto service_status = encrypt_with_service(mapped->descriptor());
auto close_status = mapped->close();
if (!service_status || close_status.is_err()) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}

auto apply_status = swapped->apply();
if (apply_status.is_err()) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}

auto context = swapped->release_completion_context();
if (!context) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}
completion_context = *context;
return ntl::flt::pre_result::success_with_callback;
```

The write post callback only releases nonpaged replacement state. Because
`release_completion_context()` prunes closed mapping records and uses nonpaged
owners/control blocks, this cleanup-only path is valid through
`DISPATCH_LEVEL`:

```cpp
auto owner = ntl::flt::swapped_io_buffers::adopt_completion_context(context);
if (!owner)
  return STATUS_INVALID_PARAMETER;
return owner->complete();
```

### Post-read decryption

Pre-read creates, installs, and transfers an output replacement. The lower file
system writes ciphertext into it. In a passive post-read worker:

```cpp
auto owner = ntl::flt::swapped_io_buffers::adopt_completion_context(context);
if (!owner)
  return STATUS_INVALID_PARAMETER;

auto mapped = owner->output()->try_map(
    crypto_connection, ntl::ipc::map_access::read_write);
if (!mapped) {
  (void)owner->complete();
  return mapped.status();
}

auto service_status = decrypt_with_service(
    mapped->descriptor(), data->IoStatus.Information);
auto close_status = mapped->close();
if (!service_status || close_status.is_err()) {
  (void)owner->complete();
  return STATUS_DATA_ERROR;
}

auto copy_status = owner->copy_back(
    ntl::flt::as_post(ntl::flt::read_callback_data{data}));
auto complete_status = owner->complete();
return copy_status.is_err() ? copy_status : complete_status;
```

`copy_back()` verifies that the typed post wrapper refers to the same callback
data, rejects error completions, and copies at most `IoStatus.Information`.
Mapping the replacement, calling a service, and copy-back remain
`PASSIVE_LEVEL` work, so read transformation must use the deferred/pending path.

The buildable
[`examples/minifilter/swap-buffers`](../../examples/minifilter/swap-buffers)
contains a smaller local-transform variant: only `.ntlxor` files use a
pre-WRITE/post-READ XOR replacement, while ordinary `.tmp` streams remain
unchanged. It demonstrates typed phases, Fast I/O fallback, PASSIVE deferral,
`IoStatus.Information` clamping, copy-back, draining cleanup, and extension
selection without the additional user-service pending state. XOR is used only
to make the swapped bytes visible; the sample documents why it is not a
production encryption design.

## Pending round trips and teardown

`pending_pre_operation_queue` is initialized per instance. It uses `FltCbdq`
for IRP cancellation, a spin lock for Filter Manager's DISPATCH-level queue
callbacks, and a system thread for all mapping/swapping cleanup at
`PASSIVE_LEVEL`. Store the queue itself in nonpaged instance/driver state; its
cancel-visible request records and control blocks are allocated from nonpaged
pool:

```cpp
auto request_id = pending_pre.pend(
    ntl::flt::as_pre(data), std::nullopt, std::move(swapped));
if (!request_id)
  return ntl::flt::pre_result::complete;

send_request(*request_id);
return ntl::flt::pre_result::pending;

// Service reply / timeout:
pending_pre.resume(id);
pending_pre.cancel(id, STATUS_IO_TIMEOUT);
```

`pending_post_operation_registry` owns post callback data while the service
modifies output. A successful reply closes mappings, copies valid output,
restores IOPB pointers, and calls `FltCompletePendedPostOperation`.

Both registries use fixed-width request IDs with monotonic generations. Stale
replies are rejected. Timeout, cancellation, disconnect, service exit, instance
teardown, and unload must all converge on `cancel`, `shutdown`, and connection
close. Paging I/O round trips are rejected by default; swapping can be opted in
for kernel-local work, but user mapping remains disabled for paging I/O.

Detached work associated with a minifilter instance must use
`queue_instance_work_item(instance, callable)`. It allocates and queues a
Filter Manager generic work item rather than a raw executive work item, so the
filter rundown reference is retained until the callable wrapper returns.
Retain an independent typed context owner for the worker with
`context_ref::reference()`:

```cpp
auto worker_context = instance_context.reference();
auto queued = ntl::flt::queue_instance_work_item(
    instance_context->instance,
    [owner = std::move(worker_context), request_id] {
      owner->pending_pre.resume(request_id);
    });
```

If queueing fails, the caller still owns its original context reference and
must cancel the registry entry before returning `STATUS_PENDING`.

If unmap reports an error and `has_open_mappings()` (or
`has_open_user_mappings()`) is still true, the pending registries do not
complete the I/O: doing so could end a borrowed MDL or original-buffer lifetime
while a user VAD still exists. The request ID becomes retryable. Close the
connection's process registry, then call `resume()`/`cancel()` or
`reply()`/`cancel()` again with the same ID. Shutdown likewise refuses to
force-complete a request with a live mapping.

## User-mode validation

Do not cast `mapped_address` directly. Validate the descriptor and the session
generation first:

```cpp
ntl::ipc::mapped_client_buffer buffer;
auto result = ntl::ipc::mapped_client_buffer::open(
    descriptor, negotiated_generation, buffer);
if (result != ntl::ipc::validation_status::success)
  return protocol_error();
```

The wrapper validates mapping ID/generation, reserved fields, access, pointer
width, and range overflow. `writable_data()` is null for a read-only mapping.

## Lifetime and IRQL contract

The required order is:

1. stop user access and receive/cancel the service request;
2. close the target-process mapping;
3. run staging or swapped-output copy-back;
4. restore original IOPB fields;
5. release owned MDLs and isolated pages;
6. resume or complete the I/O.

Mapping, target-process unmapping, page allocation, copy-back, and service waits
are `PASSIVE_LEVEL` work. A transferred swapped context with no live mapping can
perform cleanup-only `complete()` through `DISPATCH_LEVEL`; its backing and
control blocks are explicitly nonpaged. Do not let a borrowed MDL mapping
outlive its IRP or Filter Manager operation.

## Runtime and Driver Verifier fixtures

The regular WDM driver/app test exposes a real page-isolated mapping to the
calling process. It verifies user read/write, kernel observation, explicit
close, forced cleanup on handle close, process-termination rundown, generation
change, address invalidation, and automatic BUFFERED/IN_DIRECT/OUT_DIRECT/
NEITHER semantics with staging copy-back. The buffered case also verifies that
an output capacity larger than its input has a zeroed staging tail. Run it through
`scripts/ci/Run-DriverTests.ps1`; `-RequireVerifier` refuses to run unless the
driver is an active Driver Verifier target.

The dedicated minifilter pair under `test/flt/runtime/io_buffer_*` verifies
pre-write replacement and post-read copy-back through a real connected user
service. The port connect callback captures that service's process; each
replacement is mapped into it, a typed `mapped_buffer_descriptor` is sent to a
user request handler, and only the logical input/completed-output range is
mutated. The driver synchronously closes the mapping before installing input
or copying output, and the app verifies that the resulting VAD is invalid once
the I/O returns. It also verifies zero-byte EOF completion, service-disconnect
rejection, unload/reload lifetime, and the ciphertext persisted below the
filter. Deterministic gates additionally hold both pended pre-WRITE and
post-READ with a live mapped replacement. The app exercises service timeout,
active port disconnect, and filter teardown for each direction and requires
failed I/O plus VAD invalidation; timeout and disconnect also prove pending
registry counters and worker state drain.

Target IRP pre callbacks and successful read post callbacks are deliberately
pended even if they arrive at PASSIVE_LEVEL, so a passing run necessarily
traverses both deferred completion helpers. Fast I/O is disallowed and retried
on the IRP path when the system emits it. Its elevated runner is
`scripts/ci/Run-FltIoBufferRuntimeTests.ps1`. These tests require a disposable,
test-signing-enabled VM; compiling the fixtures does not count as executing
their kernel assertions.
