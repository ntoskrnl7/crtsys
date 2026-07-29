# NTL Minifilter Runtime Test

This is a VM integration fixture, not an onboarding example. It deliberately
combines operation-typed callbacks, cached file names, four Filter Manager
context lifetime units, typed RAII completion state, conditional WhenSafe
processing, and two named filter instances attached to the same volume.

The automatic instance uses development altitude `370030.227`. The app
attaches the secondary definition at `370030.228`, performs typed
communication-port and shared-ring checks, then performs create, read, write,
rename, cleanup, and close operations, and then detaches it. Communication
coverage includes contract discovery and automatically derived schema validation,
pre-decode authorization, concurrent
async calls, cooperative cancellation, transient and replayable notifications,
bounded reliable-queue quotas, typed stream upload/download and batching,
explicit session preserve/resume with ACK replay, cross-channel priority,
cancelled notification-wait cleanup, connection/session rejection quotas,
reliable byte quotas, malformed and oversized framing, invalid shared-region
range/access/quota tokens, and stale shared-region tokens. A stream facade is
also returned from a temporary client to verify that it retains the underlying
connection. The C++20 build also executes typed `co_await` calls for a method,
notification, and stream receive. The driver records context and operation
counters during unload.

The runtime app attaches and detaches the secondary instance with an explicit
altitude. The separate security probes verify that the administrators
security descriptor allows an administrator and rejects a temporary
non-administrator account. The normal app also verifies that a mismatched
schema hash is rejected before normal traffic.

Ownership coverage also exercises four Microsoft sample mechanisms in the
loaded driver:

- a transaction context is created and enlisted for transacted creates, with
  commit-finalize, rollback and destructor counters;
- an NTFS/ReFS instance registers for data scanning, creates and maps a
  read-only section, receives an overwrite conflict, closes the section from
  the notification callback and destroys its section context;
- generated callback data performs synchronous and asynchronous typed file
  queries, then holds a directory-notify request pending, cancels it through
  `async_callback_data_operation`, observes `STATUS_CANCELLED`, releases the
  handle and unloads cleanly;
- an app-issued overlapped directory notification requests a typed lower-stack
  operation-status callback, observes `STATUS_PENDING`, validates the
  read-only directory-control snapshot, destroys its nonpaged request state
  exactly once, cancels the notification and unloads cleanly.

The generated-I/O test intentionally does not use
`FltSetCancelCompletion`. That routine expects an existing incoming IRP being
posted by a minifilter; generated callback data does not have its backing IRP
until `FltPerformAsynchronousIo` submits it.

The create path uses `on_with_completion<T>()` with a flags-less normal post.
The read path uses the same typed state with a flags-less immediate post that
returns `post_continuation::when_safe`; the safe callback receives the same
state at `IRQL <= APC_LEVEL`. NTL destroys each object after direct or safe
processing, when scheduling fails, or directly while draining. The unload log
reports created, observed, and destroyed counts so the VM run exposes lifetime
regressions without turning the onboarding sample into a stress test.

The same CMake project also builds seven isolated feature fixtures. The
I/O-buffer pair is documented in
[`IO-BUFFER-README.md`](IO-BUFFER-README.md). The NameChanger pair exercises
typed name-provider registration, create redirection, name generation,
normalization, directory graft visibility, hard-link information, and the
name-bearing USN/Find-by-SID/cluster FSCTL result families; see
[`NAME-CHANGER-README.md`](NAME-CHANGER-README.md). The SimRep pair exercises
typed simulated reparses, network-query-open fallback, rename/hard-link
destination repair, and tunneled-name ownership; see
[`SIMREP-README.md`](SIMREP-README.md). The delete pair exercises typed
legacy/extended dispositions, create-time delete-on-close, cleanup
confirmation, forced disposition races, and alternate-stream classification;
see [`DELETE-README.md`](DELETE-README.md). The Scanner/AvScan pair composes
post-create cancellation, pended typed write policy, cleanup rescanning,
data-scan sections, and TxF notifications; see
[`SCANNER-README.md`](SCANNER-README.md). The MetadataManager pair exercises
per-volume metadata ownership across implicit/explicit locks, snapshots,
dismount, detach, and remount; see
[`METADATA-README.md`](METADATA-README.md). The CDO pair verifies a
user-openable legacy control device owned by the minifilter startup/unload
lifetime; see [`CDO-README.md`](CDO-README.md).

## Build

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64 -A x64
cmake --build test\flt\runtime\build_x64 --config Debug
```

## Disposable VM execution

Run every kernel fixture in a disposable, test-signing-enabled VM with a
kernel debugger available. For the selected fixture:

1. stage its `.sys`, INF, catalog or test-signing certificate, and application;
2. install and load the minifilter using the staged INF and service name;
3. enable Driver Verifier when the target guide requires it;
4. run the application against an explicitly selected disposable NTFS or ReFS
   volume;
5. unload and remove the service and test instance; and
6. verify application success, driver unload, crash/dump absence, and
   restoration of the guest's prior Verifier configuration.

Host paths, VM products, credentials, and guest staging roots are parameters of
the test environment; none are fixed by these fixtures.
