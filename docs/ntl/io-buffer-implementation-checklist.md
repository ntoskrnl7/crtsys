# I/O buffer mapping and swapping implementation checklist

This file is the persistent completion ledger for the NTL implementation. An
item is checked only after code and proportional build/test evidence exist.

## Completed

- [x] Process-scoped `MmMapLockedPagesSpecifyCache(UserMode)` bridge uses the
  WDK's real `KAPC_STATE`, process-exit synchronization, NX, and optional RO.
- [x] Connection registry assigns mapping ID/generation and rejects mappings
  after synchronous disconnect rundown begins.
- [x] Explicit mapping close invalidates copied views, unmaps in the target
  process, unregisters, then releases owned MDL/staging storage.
- [x] Isolated SystemBuffer staging remains alive until mapping close; output
  copy-back cannot run while the user mapping remains open.
- [x] METHOD_NEITHER page probing runs in the original IRP requestor process.
- [x] Completed READ/IOCTL output can be clamped to `IoStatus.Information`.
- [x] METHOD_IN_DIRECT treats its direct MDL as input and exposes the separate
  system control header as `control_input()`.
- [x] Minifilter mapping has phase-typed `as_pre`/`as_post` entry points and
  adapters for read/write, file information, directory, EA, volume, security,
  FSCTL, and IOCTL mappings.
- [x] The raw callback-data/phase implementation is internal-only. Public
  minifilter mapping requires a typed pre/post operation, public swapping
  requires a typed pre-operation, and compile assertions cover every supported
  operation type while rejecting raw and post-swap calls.
- [x] Swapped mapping must close before input apply or output copy-back;
  destruction/complete closes remaining mappings, while a live-unmap failure
  is reported and retained by pending state instead of completing unsafely.
- [x] Fast I/O swapping is explicitly rejected so the caller can disallow
  Fast I/O and retry on the IRP path.
- [x] FSCTL/IOCTL swapping handles buffered shared backing, direct control
  input, METHOD_IN_DIRECT payload input, METHOD_OUT_DIRECT output, and neither
  input/output with symmetric apply/restore adapters.
- [x] Typed post-operation copy-back rejects failed operations, clamps to
  `IoStatus.Information`, and refuses copy-back while user mapping is open.
- [x] Raw minifilter input/copy-back is performed while attached to the
  Filter Manager requestor process when no MDL system address exists.
- [x] User-mode `mapped_client_buffer` validates generation, access, reserved
  fields, pointer width, and range overflow before exposing data; x64 and x86
  unit tests cover valid, stale, read-only, invalid-access, and overflow cases.
- [x] Filter Manager receives a separate replacement MDL and owns only that
  partial MDL; NTL retains its page-isolated source MDL, preventing
  double-free and preserving the original locked-page description.
- [x] Noncached read/write replacement capacity is rounded to the target
  volume sector size while the public logical view and copy-back stay bounded
  by the original request length.
- [x] Mapping/swapping adapters cover create EA and query/set quota in addition
  to read/write, information, directory, EA, volume, security, FSCTL, IOCTL.
- [x] Warning completions such as `STATUS_BUFFER_OVERFLOW` preserve and expose
  `IoStatus.Information` bytes instead of discarding valid partial output.
- [x] Kernel-requestor and nonpaged-pool MDLs use isolated staging in automatic
  adapters so page-granular user mappings cannot expose adjacent kernel data.
- [x] Connection, staging, and replacement quotas bound retained VADs and
  isolated physical storage; paging I/O round trips are rejected by default.
- [x] Connection mapped-byte quota charges the complete page-rounded VAD span,
  including MDL byte offset, rather than only its logical byte count.
- [x] Staging and swapped-replacement quotas likewise charge page-rounded
  physical allocations, including each independent logical buffer.
- [x] Connection and pending-request generations use process-wide monotonic
  seeds so reconnect/reconstruction cannot immediately alias stale wire IDs.
- [x] `FltCbdq` list protection is DISPATCH-safe, forced cleanup is deferred to
  PASSIVE, insert/cancel races preserve pending ownership, and post registry
  shutdown waits for replies already removed from the table.
- [x] Filter Manager deferred-I/O helpers bridge pre/post callbacks to a
  PASSIVE worker before mapping, IPC, or registry work.
- [x] Concurrent explicit close and connection disconnect wait through unmap,
  staging copy-back, owned-resource cleanup, and registry removal.
- [x] Automatic IRP/minifilter mapping exposes an original user MDL zero-copy
  only when the logical range exactly covers complete pages; partial pages use
  isolated staging unless a trusted-service policy explicitly opts in.
- [x] Generic IRP mapping covers buffer-bearing user/kernel FSCTL minors without
  interpreting mount/verify/load requests as the common FSCTL layout.
- [x] Directory adapters cover extended change notification, and minifilter
  FSCTL adapters reject non-buffer-layout minor functions.
- [x] Every supplied/refreshed MDL is checked for resident-page flags and byte
  count before mapping or copying through its system address.
- [x] Resource-exhaustion paths have a single owner for process references,
  page MDLs, system mappings, and `shared_ptr` control-block failures.
- [x] Process-termination rundown does not free MDL backing merely because exit
  synchronization failed; it unmaps in the current process or waits for remote
  address-space teardown.
- [x] Swapped copy-back verifies callback-data identity, and `complete()`
  reports mapping-close failures instead of silently discarding them.
- [x] Swapped completion contexts, page owners/control blocks, mapped-state weak
  controls, and CBDQ-visible pre-state are explicitly nonpaged. Cleanup-only
  post completion is safe through DISPATCH after mapping records are pruned.
- [x] Connection rundown preserves the first cleanup failure while continuing
  to close every mapping that made forward progress; only a live unmap failure
  stops the pass for an explicit retry.
- [x] Connection rundown also waits for mapping attempts that created a user
  VAD but have not reached registry insertion, so disconnect cannot return
  before a concurrently rejected mapping has been unmapped.
- [x] Filter Manager communication connections automatically retain the
  connecting process and its mapping registry; per-port VAD limits are
  configurable, and disconnect closes the registry before invoking the
  observer. Minifilter callers use `mapping_process()` rather than recapturing
  current process state or looking up a later PID.
- [x] Buffered staging separately tracks capacity, initialized input bytes,
  and writable copy-back bytes; a larger output capacity is zero-filled rather
  than copied from an uninitialized SystemBuffer tail.
- [x] The WDM runtime fixture now drives an actual target-process VAD through
  user read/write, kernel observation, explicit close, handle cleanup,
  abrupt process exit, generation invalidation, idempotent repeated rundown,
  and all four IOCTL methods.
- [x] The WDM kernel fixture also validates automatic READ/WRITE input/output
  shape, access rights and staging copy-back, partial warning completion
  clamping and untouched output tails, hard-error suppression, buffered FSCTL
  shared backing, and rejection of non-buffer FSCTL minor functions.
- [x] A dedicated minifilter runtime pair exercises pre-write replacement,
  maps both replacement directions into a connected user service, performs
  pre-write encryption/post-read decryption there, closes and invalidates the
  VAD before apply/copy-back, exercises Fast I/O retry and PASSIVE deferral,
  rejects I/O after service disconnect, verifies unload/reload ownership,
  checks the ciphertext below the filter, and actively cancels both pre-WRITE
  and post-READ while each is pending for service timeout, port disconnect,
  and instance teardown.
- [x] Elevated runners can require that each runtime driver is already an
  active Driver Verifier target instead of silently running without Verifier.

## Completion gates

- [x] Add pending pre/post operation and request-registry state machines for
  reply, timeout, cancellation, disconnect, process exit, and unload.
- [x] Add compile coverage for typed phases, `control_input`, all operation
  adapters, deferred continuations, and explicit apply/close sequencing.
- [x] Implement runtime mapping, copy-back, disconnect/cleanup, process-exit,
  swap, Fast I/O, deferral, and verifier-gated execution fixtures.
- [x] Execute the WDM and minifilter runtime fixtures under Driver Verifier on
  a test-signing-enabled kernel test machine.
- [x] Reconcile public documentation and examples with the final API.

## Newly discovered and resolved autonomously

- Direct IOCTLs can contain both a control header and a direct payload; a
  single `input()` cannot represent METHOD_IN_DIRECT without losing semantics.
- Returning a weak mapping view without an explicit close operation caused
  mappings to survive until disconnect; copied views now share explicit close.
- Staging ownership held by a builder argument ended before the actual mapping;
  ownership is now retained by the registered mapping state itself.
- Swapped input could otherwise be installed while user mode still modified it,
  and output could copy back while user mode still modified it; both are now
  rejected until the mapping is closed.
- Minifilter post callbacks are not guaranteed to execute in the originating
  process context; raw-pointer copies now attach to `FltGetRequestorProcess`.
- METHOD_BUFFERED control requests have one physical pointer for input and
  output. They now use one isolated backing allocation and mandatory output
  tracking even when only input transformation was requested.
- A copied `swapped_buffer_view` could open an untracked second user mapping
  and bypass `apply()`/copy-back exclusion. The view is now move-only.
- Filter Manager frees a swapped-in MDL after post-operation. NTL now creates
  a distinct partial transfer MDL derived from the locked backing MDL instead
  of handing it the backing MDL it still owns or rebuilding its PFN relation.
- Filesystems may access sector-rounded noncached buffers, and warning status
  can still carry valid bytes. Capacity and completion-status handling now
  follow those contracts.
- FAST_MUTEX was not a valid cancel-queue lock at DISPATCH_LEVEL. The CBDQ uses
  a spin lock while all heavyweight cleanup stays on its PASSIVE worker.
- Per-object generation values restarted at one and could alias after a new
  connection/registry was constructed. Generations are now globally issued.
- Registry removal preceded staging copy-back, allowing disconnect rundown to
  return too early. Removal and close-event signaling now occur last.
- A descriptor length does not constrain the page-granular VAD created from an
  MDL. Automatic adapters now isolate partial first/last pages by default; the
  old behavior is an explicit trusted-service opt-in.
- `shared_ptr(raw_page_owner)` and a temporary process owner both had rare
  allocation-failure double-release paths. Ownership now remains singular
  through every throwing construction step.
- Treating `STATUS_PROCESS_IS_TERMINATING` as proof that the user VAD was gone
  could release physical backing too early. Close now unmaps locally or waits
  for remote process termination before cleanup.
- Generic IRPs omitted FSCTL, extended directory notification was omitted, and
  non-buffer FSCTL minors could be decoded through the wrong union. The adapter
  matrix and minor-function gates now cover those cases explicitly.
- A buffered control operation with no input could be swapped even when output
  swapping was disabled because selection used policy flags without checking
  which logical ranges existed. Selection now uses both policy and length.
- A typed post wrapper for unrelated callback data could drive copy-back using
  another operation's status. Copy-back now requires exact callback identity.
- Requiring every swapped-context destruction to be PASSIVE left no safe
  cleanup path when post deferral failed or completion arrived at DISPATCH.
  Transferred cleanup-only state is now wholly nonpaged and DISPATCH-safe;
  mapping, service work, and copy-back remain PASSIVE-only.
- A completed hard-error IRP could expose `IoStatus.Information` as output, and
  read-only staged output could still be copied back. Hard errors now suppress
  output, warning data remains valid, and staging copies back only writable
  logical views.
- The initial validation had only kernel-internal mapping lifetime checks and
  user-side descriptor validation; it did not prove that a real user VAD was
  writable and then forcibly invalidated. The WDM runtime protocol now proves
  that sequence and its cleanup/process-exit variants.
- `mapped_buffer` accidentally depended on NTIFS declarations inherited from
  another translation unit (`PsGetCurrentProcessId` and FSCTL minor names).
  Process-ID lookup now crosses the opaque NTIFS bridge, and the WDM-safe IRP
  adapter uses its own documented minor values.
- A disconnect could observe an empty registry while another thread had
  already created a user VAD but had not registered it. Connection close now
  disables admission and waits on an in-flight mapping-attempt rundown event.
- Pre-completion METHOD_BUFFERED staging copied `max(input, output)` bytes even
  though only the input prefix was guaranteed initialized. Copy-in and
  copy-back lengths are now independent, and a runtime case covers a larger
  zeroed output tail.
- Filter Manager presents original operation parameters to post callbacks.
  Completion-context cleanup now leaves those restored IOPB fields untouched;
  only pre-release failure performs an explicit restore and dirty notification.
- A zero-byte output tried to resolve an original destination unnecessarily.
  Zero-byte copy-back now succeeds without touching either buffer, and the
  minifilter runtime fixture explicitly drives that branch on an EOF
  completion while preserving the original EOF status.
- Caller-supplied noncached alignment could exceed `ULONG` and underflow the
  rounding bound. Alignment and addition are now range-checked before capacity
  allocation.
- Mapped-byte quota counted `MmGetMdlByteCount` even though a user VAD consumes
  complete pages. Registry accounting now uses the page-rounded MDL span and
  rejects a span that cannot be represented by the target architecture.
- Staging/replacement limits compared logical byte lengths while isolated
  backing consumes complete pages. Their admission checks now charge the same
  page-rounded physical amount that is actually allocated.
- `RequestorMode == UserMode` and an absent nonpaged-pool flag were not enough
  to prove an MDL described a user virtual range. Zero-copy classification now
  also requires the complete MDL virtual range to stay below
  `MmSystemRangeStart`; otherwise it uses isolated staging.
- The original validation proved WDM user mapping and minifilter swapping in
  separate fixtures, but not the intended combined service round trip. The
  minifilter fixture now captures the communication-port process, maps each
  swapped replacement into it, sends a typed fixed-width descriptor, lets the
  app mutate only the logical valid range, closes the mapping before
  apply/copy-back, verifies VAD invalidation, and fails I/O when the service is
  disconnected.
- The first integrated fixture still captured the port process manually in its
  `on_connect` hook. That left an easy-to-miss lifetime rule in application
  code. Process capture, configurable mapping quotas, and disconnect rundown
  now belong to `communication_connection`; the fixture consumes the same
  production API.
- Once a communication facade owned the process registry, its last copied
  facade could be released above APC after disconnect. The process registry
  now records one close transition and its first rundown result; an already
  empty/closed registry can be destroyed without reacquiring a PASSIVE-only
  lock, while destruction with a live VAD still asserts the required
  PASSIVE-level ownership contract.
- A zero-byte direct/neither control request could previously return a
  successful `mapped_io_buffers` containing no logical views. Automatic IRP
  mapping now rejects operations with neither input/control-input nor visible
  output, matching the zero-length read policy.
- Pending pre/post cleanup used to propagate an unmap error but still complete
  the I/O, which could end a borrowed MDL or original staging destination
  while its VAD remained live. Both registries now distinguish a cleanup error
  from an actually open mapping, retain the operation under the same request
  ID, permit a disconnect-close followed by retry, preserve the first cleanup
  failure, and refuse unsafe force-completion during shutdown.
- The final process-registry owner likewise no longer releases backing after
  an exceptional live-unmap failure. At PASSIVE it retains the registry until
  target-process teardown removes the VAD, then makes a final close pass;
  destroying a genuinely live registry above PASSIVE remains an asserted
  caller contract violation.
- The initial dedicated minifilter fixture exercised Filter Manager deferral
  but called the service synchronously inside its deferred routine, leaving
  the normal pending registries compile-only. WRITE now transfers ownership to
  `pending_pre_operation_queue` and resumes after user transformation; READ
  transfers post ownership to `pending_post_operation_registry` and replies
  after transformation. Detached work explicitly acquires a second strong
  instance-context reference, so teardown cancellation cannot leave a late
  worker with a raw context pointer or make queue-failure cleanup dereference a
  context released with the failed closure.
- A strong instance-context reference protects state but does not itself pin
  the minifilter image containing a raw `ExQueueWorkItem` callback.
  `queue_instance_work_item` now uses `FltQueueGenericWorkItem`, whose Filter
  Manager rundown reference lasts until the worker routine returns. The
  pending WRITE/READ service workers use that helper, so active filter unload
  cannot race their code epilogue.
- The same raw executive-worker issue existed in the Filter Manager
  communication layer's async method, transient notification, targeted
  notification, and reliable-delivery paths. They now use
  `queue_filter_work_item`; connection rundown continues to protect state while
  Filter Manager rundown independently protects the executing image.
- Copy-capturing notification payloads or reliable-delivery sequence vectors
  while forming a lambda could allocate outside the `noexcept` queue helper and
  terminate the kernel on resource exhaustion. Each queued batch now has one
  allocation-checked shared immutable owner; queue failure can still inspect
  the same sequence list to clear `in_flight` records and release connection
  rundown exactly once.
- The first pending-registry runtime proof covered normal resume/reply and I/O
  submitted after an already completed disconnect, but not cancellation while
  an operation and VAD were simultaneously live. Fixed-width test controls now
  hold pre-WRITE and post-READ at deterministic kernel/user gates and verify
  timeout, disconnect, and teardown cancellation in both directions, registry
  drain, I/O failure, VAD invalidation before any late user handler returns,
  and absence of lower-file side effects from canceled pre-WRITEs.
- A post registry that had already completed PASSIVE shutdown still asserted
  PASSIVE when its containing Filter Manager context was later reclaimed.
  Shutdown completion and reopen are now serialized and recorded, allowing
  cleanup of an already-empty registry without repeating PASSIVE-only work.
- The first WDM runtime attempt under Standard Driver Verifier exposed an
  unrelated pre-existing `lookaside_list<T>` defect before the I/O-buffer
  fixture could run. `ExInitializeLookasideListEx` received `sizeof(T)` even
  when it was smaller than the native `SLIST_ENTRY` free-list node, producing
  verifier bugcheck `0xC4/0xCD` for a four-byte test object on x64. Native
  entry storage is now `max(sizeof(T), sizeof(SLIST_ENTRY))`, while the public
  allocation remains typed as `T`.
- The next WDM runtime attempt reached the IOCTL pipeline but its regression
  fixture built a nonpaged-pool MDL over an eight-byte kernel-stack scratch
  object. Kernel stacks are tradable memory and are not a valid
  `MmBuildMdlForNonPagedPool` backing, so Driver Verifier stopped it with
  `0xC4/0x140`. The fixture now owns an RAII nonpaged-pool scratch allocation
  until after the describing MDL is freed, and the low-level MDL wrapper
  documents the backing-memory precondition explicitly.
- A later pass found the same invalid assumption in the standalone MDL wrapper
  test: its 64-byte sample range was also an expanded kernel-stack local.
  Driver Verifier again reported `0xC4/0x140`; the test now keeps an RAII
  nonpaged-pool allocation alive until its released native MDL is freed.
- The following verified load exposed a crtsys shared-compiler-TLS defect
  before the I/O fixture entered DriverEntry. A `KSPIN_LOCK` and the compiler
  TLS slot vector lived directly in a pageable named-section view; acquiring
  the lock raised to DISPATCH_LEVEL and faulted on its invalid prototype PTE,
  producing bugcheck `0xA`. Shared-runtime setup/teardown is now serialized by
  a named synchronization event, the section pages are locked by an owned
  MDL, and all elevated-IRQL access uses the MDL's separate nonpaged system
  mapping. The canonical view, mapping MDL, and section backing are retained
  until the final crtsys-linked driver reference releases them.
- The next verified load exposed a remove-lock lifetime violation in the WDM
  regression tests. The IOCTL pipeline called
  `IoReleaseRemoveLockAndWait` on a stack-local lock, then
  `ntl_remove_lock_test` initialized another lock after the expanded kernel
  stack reused the same address. Driver Verifier reported `0xC4/0xD7`, with
  the remove-lock address inside the current thread's stack limits. Both
  single-shot tests now use distinct driver-image-lifetime locks, and the
  public wrapper documents that remove-lock storage must be stable,
  nonpaged, and never reconstructed or reinitialized at the same address.
- The corrected WDM rerun advanced past the remove-lock test, then the VMware
  guest received the environment's previously documented external-NMI
  signature: bugcheck `0x80` with parameters `0x004f4454, 0x10, 0, 0`.
  The live WHEA record contained only a fatal NMI section, with zero MCE/CMC
  errors; the interrupted CPUs were executing ordinary test-thread teardown
  and Special Pool frees, and no Verifier or driver-fault path was present.
  This stop is tracked as VM/KD noise rather than an implementation defect;
  subsequent reruns suppress the known LDK debugger-output path.
- Enabling a new target and Standard-like flags through volatile Driver
  Verifier state on the Windows 11 22000 test guest caused an operating-system
  `0xA` in `nt!VF_FIND_DEVICE_INFORMATION_AND_REMOVE` before the target driver
  was loaded. Volatile activation is excluded from this validation flow; final
  passes use boot-time Standard settings only.
- `ntl::status` converts to its underlying `NTSTATUS`, whose success value is
  zero; treating it as a Boolean therefore inverted success and failure. The
  mapping, swapping, pending-I/O, runtime-fixture, and connection-close paths
  now use explicit `is_ok()`/`is_err()` tests. The verified WDM fixture then
  passed all METHOD_BUFFERED, IN_DIRECT, OUT_DIRECT, and NEITHER mapping and
  copy-back cases.
- The first verified minifilter write swap exposed `0x12E
  (INVALID_MDL_RANGE)` in `IoBuildPartialMdl`. `page_buffer` is backed by an
  `MmAllocatePagesForMdlEx` MDL whose `StartVa` can be null while
  `MappedSystemVa` is non-null; the partial-MDL range must be expressed relative
  to `MmGetMdlVirtualAddress(SourceMdl)`, not the temporary kernel mapping.
  Replacement-MDL construction now uses the source-MDL virtual range while the
  page-isolated system mapping remains the actual replacement buffer pointer.
- The corrected partial-MDL run then pended the first WRITE indefinitely even
  though the user transform and `pending_pre_operation_queue::resume` both
  reported success. Live callback data showed the IRP removed from CBDQ with
  `finish_phase == 2`, no completion context, and no call into the lower stack.
  `claim_finish()` returns `ntl::status`; testing it with `if (!claimed)`
  interpreted `STATUS_SUCCESS == 0` as true and returned before `apply()`,
  `release_completion_context()`, and `FltCompletePendedPreOperation`.
  Resume/cancel now use explicit `is_ok()`, and the same missed status inversion
  in per-instance post-registry initialization is corrected. `ntl::status`
  additionally deletes contextual Boolean conversion, and a compile-time
  regression assertion prevents future `if (status)`/`if (!status)` use.
- The first completed minifilter runtime pass reached unload, where Filter
  Verifier warned that it could not identify the calling filter for exactly two
  generic-work-item releases and consequently retained two stale
  `FLT_GENERIC_WORKITEM` tracking records. Both recorded object addresses were
  already freed and reused under unrelated pool tags. Release disassembly
  showed every `generic_work::invoke` ending in a tail `jmp` to
  `FltFreeGenericWorkItem`, which removed the driver return address Filter
  Verifier uses for attribution. All releases now pass through a non-inlined
  wrapper with an instruction after the Filter Manager call, preserving a
  driver return address without changing work-item ownership.
- Continuing that run exposed bugcheck `0xCB`
  (DRIVER_LEFT_LOCKED_PAGES_IN_PROCESS)` when the user test process exited.
  `!lockedpages <process>` identified seven MDLs and exactly eleven locked
  pages, all allocated through `FltLockUserBuffer` at the pre-WRITE and
  pre-READ swap preparation sites. Those calls installed an MDL for the
  original user buffer and `apply()` immediately overwrote the same IOPB field
  with the replacement MDL, leaving the intermediate MDL outside both the
  original-parameter and swapped-MDL cleanup paths. Swap preparation no longer
  locks a missing original MDL: input is copied under guarded requestor-process
  attachment, output is untouched before the lower file system, and output
  copy-back locks a missing original MDL only after Filter Manager has restored
  the post-operation parameters. The post-created MDL remains in callback data
  for Filter Manager to unlock and free on completion. Copy-back also rejects a
  refreshed original MDL whose described output range is shorter than the
  number of bytes being returned.
- The communication round trip initially treated `FltSendMessage`'s
  success-severity `STATUS_TIMEOUT` as a successful reply and treated a late
  user `FilterReplyMessage` with `ERROR_FLT_NO_WAITER_FOR_REPLY` as a fatal
  receiver error. Kernel timeout is now normalized to `STATUS_IO_TIMEOUT`,
  while a stale reply is discarded without terminating unrelated receive
  workers.
- The WDM verifier run spent nearly 46 minutes in the cppreference PMR
  benchmark because its default configuration performed 80 million list-node
  insertions through verified pool allocation. Normal driver tests now use a
  bounded semantic workload; the original stress scale remains available only
  through the explicit `CRTSYS_TEST_EXTENDED_PMR_BENCHMARK` CMake option.
- The first complete WDM function pass then reached driver unload and Driver
  Verifier stopped with `0xC4/0x62` for two nonpaged allocations totaling
  32 bytes. Walking `_VI_POOL_ENTRY` records identified two live `LdCs`
  special-pool blocks, both allocated by LDK's `RtlEnterCriticalSection` and
  referenced by UCRT lock-table entries 4 (`locale`) and 5
  (`multibyte code page`). Release builds passed `terminating=true` to
  `__scrt_uninitialize_crt`, which intentionally skips UCRT uninitializers
  under user-process termination semantics. Driver unload is not process
  termination, so teardown now always passes `false` and runs the lock
  destructors. The corrected x64 Release WDM driver subsequently completed a
  balanced Verifier load/unload without the `0xC4/0x62` stop. The dedicated
  minifilter fixture was then rebuilt against the same corrected runtime and
  also completed its self-unload cleanly under Verifier.
- Independent consumer/test builds wrote differently configured private
  `crtsys.lib` archives into the same source-tree `lib/<arch>` directory. A
  minifilter build could therefore replace the WDM test archive and cause a
  later incremental link to lose test-only symbols. Only top-level builds now
  publish to the source-tree library directory; dependency builds retain their
  archive in their own binary tree.
- The public minifilter surface still allowed callers to bypass the typed
  pre/post wrappers by naming a raw phase argument. The raw implementation and
  phase enum are now internal details, and a complete operation-type compile
  matrix proves that mapping accepts only typed pre/post operations and
  swapping accepts only typed pre-operations.
- Earlier WDM coverage proved all four IOCTL transfer methods but did not
  directly execute automatic READ/WRITE classification, warning-status
  completion clamping, or the FSCTL minor-function gate. A kernel fixture now
  checks those contracts, while the minifilter application deliberately reads
  past EOF and verifies that only the `IoStatus.Information` prefix is
  transformed and copied back.

## Validation evidence

- [x] VS 2026 / WDK 10.0.28000 x64 driver test target builds with `/W4 /WX`.
- [x] VS 2019-compatible / WDK 10.0.22621 x86 driver test target builds.
- [x] The final WDM test driver and dedicated minifilter driver/application
  build and link on x64, ARM64, x86, and ARM after the typed-surface and
  archive-isolation changes.
- [x] The final post-lock ownership, UCRT unload, and bounded-PMR corrections
  build in the v145 x64/ARM64 and v142 x86/ARM Release targets.
- [x] User-mode descriptor tests pass on x64 and x86.
- [x] Final clean `Rebuild` passed for the v145 x64 and v142/WDK 22621 Win32
  WDM driver tests plus both the dedicated I/O-buffer and general communication
  minifilter runtime pairs after all ownership, IRQL, and teardown changes.
- [x] Final x64/Win32 `ntl_ipc.*` user-mode tests passed
  (`pointer_free_region_and_bounded_ring` and
  `mapped_descriptor_validation`).
- [x] `git diff --check` and the WDK-derived hard-coded `NTSTATUS` validation
  script pass.
- [x] The new WDM end-to-end mapping and four-method IOCTL test code builds and
  links in the x64 and Win32 driver/app targets.
- [x] The dedicated swapped-buffer minifilter driver/app builds with `/W4 /WX`
  on v145 x64, v145 Win32, and the v142/WDK 22621 Win32 compatibility target.
- [x] The public communication minifilter driver/app consumes the final
  communication and work-item headers on v145 x64, v145 Win32, and
  v142/WDK 22621 Win32.
- [x] The minifilter runtime fixture deliberately pends target pre-read/write
  and successful post-read callbacks even when initially at PASSIVE, so a
  successful run necessarily traverses both deferred completion helpers.
- [x] Its normal WRITE and nonempty READ paths now additionally traverse
  `pending_pre_operation_queue::resume` and
  `pending_post_operation_registry::reply`; per-instance teardown cancels both
  registries while explicit strong context references protect late detached
  workers and Filter Manager generic work-item rundown pins their code.
- [x] Deterministic runtime controls now exercise six live cancellation paths:
  pre-WRITE and post-READ under service timeout, communication-port disconnect,
  and filter teardown. Each path requires failed I/O and invalidation of the
  mapped replacement VAD; timeout/disconnect also require the corresponding
  registry cancel counter and active-worker count to drain. A distinct canceled
  WRITE payload proves that none of the three pre-operation cancel paths reaches
  the lower file.
- [x] The same fixture's v145 x64/Win32 and v142/WDK 22621 Win32 driver/app
  matrices compile with `/W4 /WX` after integrating the real Filter Manager
  port, connected-process capture, mapped-descriptor request/reply, explicit
  unmap, and disconnect rejection.
- [x] Runtime runners parse successfully, reject this non-elevated session at
  preflight, and contain explicit test-signing, service, and optional active
  Driver Verifier target gates before loading/running a driver.
- [x] The first Win11KD VMware Standard-Verifier run produced a symbol-matched
  minidump proving `crtsys_test!ntl::lookaside_list` passed a four-byte native
  entry where x64 required eight bytes; the corrected wrapper and regression
  assertion build in the v145 x64 and v142 x86 driver targets.
- [x] The second Win11KD Standard-Verifier stop was analyzed live with private
  symbols. Bugcheck `0xC4/0x140`, the verifier-owned MDL, and the current
  thread's stack limits proved that the IOCTL fixture passed kernel-stack
  storage to `MmBuildMdlForNonPagedPool`; its RAII nonpaged-pool correction
  builds in the v145 x64 and v142 x86 driver targets.
- [x] A subsequent `0xC4/0x140` identified the same kernel-stack misuse in
  `ntl_mdl_test`; the MDL showed the 64-byte range inside the expanded stack
  limits. Its nonpaged-pool correction builds in the v145 x64 and v142 x86
  driver targets.
- [x] The third live stop was identified as bugcheck `0xA` at
  `CrtSysAllocateSharedCompilerTlsSlot`: the fault address was exactly the
  named-section runtime plus the `Lock` offset and `!pte` showed an invalid
  prototype PTE. The resident MDL-system-mapping and cross-image gate fix
  builds for `crtsys_test`, both multi-instance drivers, and their probe in
  the v145 x64 and v142 x86 matrices.
- [x] The following live stop was identified as Driver Verifier
  `0xC4/0xD7`: `VerifierIoInitializeRemoveLockEx` rejected a remove lock at an
  expanded-stack address previously used and released by the IOCTL pipeline
  test. The tests now use two unique image-lifetime locks, and the correction
  builds in the v145 x64 and v142/WDK 22621 x86 driver targets.
- [x] Re-run the corrected WDM fixture and the dedicated minifilter fixture
  under boot-time Standard Driver Verifier in the Win11KD VMware guest.
- [x] Confirm the v145 x64 Release work-item release wrapper contains a real
  `call FltFreeGenericWorkItem`, followed by a driver `nop` and `ret`.
- [x] Repeat minifilter unload and verify that Filter Verifier reports neither
  stale generic-work-item records nor a real resource leak.
- [x] Repeat the full minifilter process-exit path with locked-page tracking
  enabled and confirm `!lockedpages` has no MDL originating from pre-swap
  `FltLockUserBuffer` calls and no `0xCB` occurs.
- [x] The final x64 minifilter Verifier run completed its full 4 KiB
  encrypt/decrypt round trip, six mapped reads (including an oversized
  short-read whose sentinel tail remained unchanged), and all six live
  timeout/disconnect/teardown cancellation paths. Filter Verifier observed
  balanced load/unload cycles, no generic-work-item warning, no locked-page
  stop, and no resource leak. After rebuilding with the UCRT unload correction,
  the fixture passed again and the final post-run Verifier query reported
  `crtsys_flt_io_buffer_runtime_test.sys` load 8/unload 8 with zero tagless or
  untracked allocations.
- [x] The WDM user application completed all 15 tests, including automatic IRP
  input/output semantics and copy-back, forced unmap on disconnect, and
  process-exit close. After the UCRT termination-semantics correction,
  the augmented kernel-side READ/WRITE/FSCTL test also passed during
  `DriverEntry`; `sc stop` and service deletion both completed normally.
  Verifier reported `crtsys_test.sys` load 2/unload 2 with zero tagless or
  untracked allocations.
- [x] The public minifilter examples are split into independent `basic`,
  `communication`, and `swap-buffers` projects. The last contains a
  case-insensitive `.ntlxor` example that XORs only a pre-WRITE replacement
  and the valid
  `IoStatus.Information` prefix of a post-READ replacement. It disallows
  target Fast I/O, performs allocation/copy-back through PASSIVE deferral,
  leaves ordinary `.tmp` streams unchanged, and documents why XOR and its
  file-selection policy are not production encryption. The x64 `/W4 /WX`
  drivers/applications and all three x86 cross-bitness applications build.
  Separate x64 VM smoke runs completed the basic file lifecycle, every
  communication-port feature, the `.tmp`/`.ntlxor` swap paths, unload, and
  service deletion.
- [x] All three public sample INFs use DIRID 13 and
  `Parameters\Instances`, have distinct service/instance/altitude identities,
  and pass WDK 10.0.28000 `InfVerif /w`.
- [x] Split IRP mapping phase from policy: pre-completion callers use
  `try_map_io_buffers`, completed callers use
  `try_map_completed_io_buffers`, and `map_io_options` no longer contains a
  completion-state boolean.
- [x] Made minifilter swap selection operation-typed. READ/WRITE and other
  single-direction operations infer their only valid direction; QUERY_EA,
  QUERY_QUOTA, FSCTL, IOCTL, and internal IOCTL require `swap_buffer`.
  Compile-time tests reject the inverse overloads, policy-as-selector calls,
  unsupported operations, and any return of direction booleans in
  `swap_io_options`.
- [x] Rebuilt the WDM and minifilter runtime drivers for x64 and x86, all three
  minifilter Visual Studio examples in x64 Debug/Release, and validated all 22
  Visual Studio example projects. The WDM VM fixture passed all 15 tests using
  the completed-map entry point, and the swap-buffers VM sample passed its
  `.tmp` pass-through plus `.ntlxor` pre-WRITE/post-READ round trip, unload,
  and service deletion.

## User decisions required

None currently. Unsupported/unsafe cases use explicit status returns and are
being resolved using conservative defaults.
