# Scanner/AvScan Runtime Fixture

This isolated driver/app pair composes the reusable mechanisms demonstrated by
Microsoft's `scanner` and `avscan` samples into one typed NTL lifecycle. It is
a policy and ownership test, not a production antivirus engine. The fixture
only watches `*.scan` files under `\crtsys-flt-scanner-runtime` and scans at
most the first 4096 bytes for the test signature `CRTSYS_FOUL`.

## Files and responsibilities

| File | Responsibility |
| --- | --- |
| `scanner_shared/scanner_runtime.hpp` | Fixed-width typed RPC contract, test names, verdicts, and observable counters |
| `scanner_driver/main.cpp` | Create/write/cleanup policy, data-scan sections, pending I/O, contexts, and TxF enlistment |
| `scanner_app/main.cpp` | User-mode scanner service and end-to-end assertions |
| `scanner_driver/crtsys_flt_scanner_runtime_test.inf` | Development instance at altitude `370030.233` |

The registration boundary stays operation-typed:

- `create_callback_data` handles pre/post create;
- `write_callback_data` handles pre-write;
- `cleanup_callback_data` handles pre-cleanup;
- typed transaction and section-context callbacks handle their notifications.

The deferred PASSIVE-level routine receives native callback data because that
is Filter Manager's work-item ABI. It immediately reconstructs the known
`write_callback_data` type; this is an implementation boundary, not a raw
registration escape hatch.

## Lifecycle

| Operation | Driver action | User-visible result |
| --- | --- | --- |
| successful open | Map a read-only data-scan section, send a typed request, and call `try_cancel_file_open()` for an infected verdict | Infected existing file fails with `ERROR_ACCESS_DENIED` |
| writable open | Attach a typed stream-handle context and enlist a typed transaction context when TxF is present | Cleanup knows whether a rescan is required |
| non-paging write | Defer to PASSIVE level, copy into isolated pages with `try_swap_io_buffers()`, pend in `pending_pre_operation_queue`, and request a verdict | Clean pages resume to the lower stack; infected pages are released and the original write completes with access denied |
| cleanup | Rescan the final file contents through a data-scan section | A mapped/paging write that bypassed normal pre-write inspection is detected; cleanup itself is not failed |
| transaction finalization | Receive commit-finalize or rollback through the enlisted typed transaction context | Both TxF paths and context destruction are observed |
| scanner disconnect | Bypass create/write policy before acquiring or pending caller buffers | Bootstrapping and test cleanup are fail-open, matching the Microsoft Scanner sample's service-availability policy |

The write path keeps the isolated `swapped_io_buffers` owner inside the
cancel-safe pending queue. An allow verdict applies resident replacement pages
before resuming the lower stack. A deny, cancellation, disconnect, or teardown
releases those pages without making a stale user buffer visible to the file
system.

`FltCreateSectionForDataScan` can invoke the section-conflict callback before
the create call returns. The section context therefore owns an atomic abort
flag initialized before section creation. The general runtime fixture forces
the conflict path; this scanner fixture verifies balanced successful
create/map/close composition.

## What the app proves

The app seeds files before connecting to prove disconnected fail-open
bootstrapping, then verifies:

- clean and infected existing-file opens;
- one accepted and one rejected ordinary `WriteFile`;
- cleanup detection after a memory-mapped write;
- one committed and one rolled-back TxF write;
- typed kernel-to-user requests with no transport failures;
- balanced data-scan sections, section contexts, pending writes,
  stream-handle contexts, and transaction contexts.

A passing run currently reports the following deterministic policy counts:

```text
policy=19/0 open=10/1 write=4/3/1 cleanup=5/1
sections=8/8/8 pending=4/3/1 deferred=4
handle_contexts=5/5 transaction_contexts=2/2
enlistments=2 commits=1 rollbacks=1
```

The first number after `open` is scans and the second is denied opens.
`write` is scans/allowed/denied, `cleanup` is scans/infected, `sections` is
created/mapped/closed, and `pending` is pended/resumed/cancelled.

## Build and VM test

```powershell
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_scanner_runtime_test `
           crtsys_flt_scanner_runtime_test_app -- /m:1

powershell -NoProfile -ExecutionPolicy Bypass `
  -File D:\projects\crtsys-vm-test\Run-CrtSysMinifilterInVm.ps1 `
  -DriverPath D:\projects\crtsys\test\flt\runtime\build_x64_v145\Debug\crtsys_flt_scanner_runtime_test.sys `
  -AppPath D:\projects\crtsys\test\flt\runtime\build_x64_v145\Debug\crtsys_flt_scanner_runtime_test_app.exe `
  -ServiceName CrtSysFltScannerRuntimeTest `
  -InstanceName 'CrtSys FLT scanner runtime test instance' `
  -Altitude 370030.233 `
  -GuestDirectory C:\crtsys-minifilter-scanner-test `
  -LogPath D:\projects\crtsys\artifacts\flt-scanner-vm.log
```

For WOW64 coverage, keep the x64 driver and replace only `-AppPath` with
`build_x86_v145\Debug\crtsys_flt_scanner_runtime_test_app.exe`. Use a distinct
guest directory and log such as `flt-scanner-vm-x86-app.log`. Both runs must
end with `APP_RC=0`, `UNLOAD_RC=0`, and `PASS`.
