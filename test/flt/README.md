# NTL Minifilter Tests

This directory owns minifilter API contract and runtime fixtures that would
make the public samples harder to read.

The repository-wide mapping from Microsoft samples to NTL mechanisms lives in
the [`WDK minifilter sample coverage`](WDK-SAMPLE-COVERAGE.md) matrix. Update
that matrix only when the corresponding public API, compile contract, or
runtime evidence has changed.

`compile/registration_and_ownership.cpp` is the single-file usage contract for
name-provider registration, transaction enlist/removal, data-scan section
setup, and owned callback-data I/O. It is intentionally structured like driver
code so the ownership transitions are visible, while still compiling and
linking in the ordinary x86/x64 matrix without requiring a mounted Filter
Manager instance.

That compile fixture verifies typed callback signatures, rejection of native
`PFLT_*` callback signatures, fluent registration, context types, transaction
enlist/removal, bounded name output, data-scan setup/cleanup, typed
cancellation, and callback-data ownership transfer. It does not simulate
Filter Manager. A loaded minifilter in a disposable VM is still required to
exercise:

- name-provider cache and normalization behavior;
- Driver Verifier unload and rundown behavior.

The general runtime fixture now covers KTM commit/rollback delivery,
data-scan section conflicts and cleanup, and generated synchronous/asynchronous
I/O cancellation and teardown. It also issues a real overlapped directory
notification and verifies the typed lower-stack operation-status snapshot,
`STATUS_PENDING`, request-state destruction, cancellation and unload. Both
native x64 and WOW64 clients run against the x64 driver.

`compile/operation_callback.cpp` is compiled into the regular CMake driver
test matrix. It verifies operation-tag callback syntax, operation/callback
type rejection, operation-specific parameter access, read-only operation-status
snapshots, rejection of native operation-status callbacks, and generic lambdas
with `auto` parameters without adding test-only statements to
`examples/minifilter`. The generic form is compile-supported, while
the public sample uses explicit callback-data types because current C++ editor
completion engines do not reliably list members for contextually instantiated
generic-lambda parameters.

`compile/context.cpp` verifies typed file/stream context declarations, the
move-only Filter Manager reference owner, constructor forwarding, and the
registration surface across supported MSVC toolsets. Runtime file-system
behavior stays in the minifilter driver/app fixture rather than being mixed
into the ordinary WDM unit-test driver.

`compile/control_device.cpp` verifies that a minifilter can queue a typed
`ntl::device` without directly including `fltKernel.h` or assigning a raw WDM
dispatch table.

`compile/abi_win7_provider.cpp` and `compile/abi_win8_consumer.cpp` form a
cross-target link contract. The Windows 7 object defines a template symbol
whose arguments are `sizeof(ntl::flt::driver)` and
`sizeof(ntl::flt::registration)`; the Windows 8 object requires the symbol
computed from its own view. Linking the ordinary driver test therefore fails
if WDK version gates ever change either public prebuilt-library layout.

`compile/instance.cpp` verifies the owning instance reference, identity query,
named and altitude attachment overloads, detach surface, and filter-wide
instance enumeration across supported toolsets.

`runtime/` is a standalone driver/app integration fixture. It keeps the
multi-instance, four-context, conditional WhenSafe, draining, and explicit
detach coverage out of the onboarding example while remaining buildable and
loadable as a real minifilter in the VM. Its communication test also owns the
protocol-negative, quota, coroutine, disconnect, and unload-lifetime checks;
the onboarding sample is intentionally kept shorter.

`runtime/io_buffer_*` adds a separate filter/app pair for mapped/swapped I/O
buffers. It maps replacement pages into the process connected through the
Filter Manager port and verifies user-mode pre-write encryption, post-read
decryption/copy-back, VAD invalidation, disconnected-service rejection,
PASSIVE deferral, Fast I/O retry, unload/reload ownership, active
timeout/disconnect/teardown cancellation for both pended directions, and the
actual ciphertext persisted below the filter. See
[`runtime/IO-BUFFER-README.md`](runtime/IO-BUFFER-README.md).

`runtime/name_changer_*` adds a separate typed name-provider pair. It redirects
opens from a nonexistent visible graft to a physical backing directory,
prevents direct backing access, translates generated names, and adjusts parent
directory enumeration, hard-link queries, and name-bearing FSCTL output. The
verifier app proves the mapping before and after filter unload across NTFS/ReFS
and x64/WOW64 callers. The exact covered surface and filesystem-specific
unsupported operations are listed in
[`runtime/NAME-CHANGER-README.md`](runtime/NAME-CHANGER-README.md).

`runtime/simrep_*` is the isolated SimRep pair. It verifies phase-typed
pre-create reparsing, network-query-open Fast I/O fallback, validated
rename/hard-link destinations, lower-instance reissue, and tunneled-name
completion-state ownership with both x64 and WOW64 apps. See
[`runtime/SIMREP-README.md`](runtime/SIMREP-README.md).

`runtime/delete_*` is the isolated delete pair. It verifies copied typed
legacy/extended disposition views, create-time delete-on-close, post-cleanup
deletion confirmation, forced racing operations, pending multi-handle
deletion, and alternate-stream versus whole-file classification with x64 and
WOW64 apps. See
[`runtime/DELETE-README.md`](runtime/DELETE-README.md).

`runtime/scanner_*` is the isolated Scanner/AvScan pair. It verifies typed
driver-to-app scan requests, successful post-create cancellation, isolated
and cancel-safe pended writes, mapped-write cleanup rescanning, data-scan
section ownership, fail-open disconnect behavior, and TxF commit/rollback
delivery with x64 and WOW64 apps. See
[`runtime/SCANNER-README.md`](runtime/SCANNER-README.md).

`runtime/metadata_*` verifies per-instance metadata ownership across implicit
and explicit volume locks, snapshot update holds, invalidated old instances,
successful dismount/detach, and ReFS remount. See
[`runtime/METADATA-README.md`](runtime/METADATA-README.md).

`runtime/cdo_*` verifies minifilter-owned legacy control-device startup and
teardown, user-mode open, typed IOCTLs, optional-unload veto, continued
dispatch, cleanup/close, and reopen. See
[`runtime/CDO-README.md`](runtime/CDO-README.md).

`cross-bitness/` is a user-mode-only build fixture. Configure it as Win32 and
run its x86 app against the x64 driver produced by `runtime/`. The shared
Filter Manager port records contain fixed-width IDs, sizes, and tokens, so the
test exercises the same cross-bitness contract as the general RPC fixture.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-cross-bitness-app -Architecture x86 `
  -PlatformToolset v143 -Configuration Debug
```

The integration environment must pair the x64 `crtsys_flt_runtime_test.sys`
driver with the x86 `crtsys_flt_runtime_x86_app.exe` client. This is not a
claim that a 32-bit process can load a 64-bit driver.

`verifier-stress/` is a separate user-mode fixture for repeated communication
port close, filter unload, and filter reload cycles. It is intended to run
under an already configured Driver Verifier session and does not modify
verifier settings.
