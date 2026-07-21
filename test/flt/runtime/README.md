# NTL Minifilter Runtime Test

This is a VM integration fixture, not an onboarding example. It deliberately
combines operation-typed callbacks, cached file names, four Filter Manager
context lifetime units, typed RAII completion state, conditional WhenSafe
processing, and two named filter instances attached to the same volume.

The automatic instance uses development altitude `370030.227`. The app
attaches the secondary definition at `370030.228`, performs typed
communication-port and shared-ring checks, then performs create, read, write,
rename, cleanup, and close operations, and then detaches it. Communication
coverage includes contract discovery, pre-decode authorization, concurrent
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

The create path uses `on_with_completion<T>()` with a flags-less normal post.
The read path uses the same typed state with a flags-less immediate post that
returns `post_continuation::when_safe`; the safe callback receives the same
state at `IRQL <= APC_LEVEL`. NTL destroys each object after direct or safe
processing, when scheduling fails, or directly while draining. The unload log
reports created, observed, and destroyed counts so the VM run exposes lifetime
regressions without turning the onboarding sample into a stress test.

## Build

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64 -A x64
cmake --build test\flt\runtime\build_x64 --config Debug
```

## VM Run

Run only in the disposable test VM with WinDbg connected:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File D:\projects\crtsys-vm-test\Run-CrtSysMinifilterInVm.ps1 `
  -DriverPath D:\projects\crtsys\test\flt\runtime\build_x64\Debug\crtsys_flt_runtime_test.sys `
  -AppPath D:\projects\crtsys\test\flt\runtime\build_x64\Debug\crtsys_flt_runtime_test_app.exe `
  -ServiceName CrtSysFltRuntimeTest `
  -InstanceName 'CrtSys FLT runtime test instance' `
  -Altitude 370030.227 `
  -SecondaryInstanceName 'CrtSys FLT runtime test secondary instance' `
  -SecondaryAltitude 370030.228 `
  -LogPath D:\projects\crtsys\artifacts\flt-runtime-vm.log
```

Success requires the app to return zero, the secondary instance to disappear
after the app exits, and the filter to unload without a debugger breakpoint or
bugcheck.
