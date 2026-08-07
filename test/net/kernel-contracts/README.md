# NTL kernel networking contracts

[한국어](./README.ko-KR.md)

This contract test makes the dual-runtime boundary executable rather than
presenting only a code fragment. The app sends bounded HTTP/1, HTTP/2, HTTP/3, gRPC,
WebSocket, WebTransport, QPACK, and TLS ClientHello inputs through a fixed IOCTL
contract. The driver validates them with the same `ntl::net` APIs. It also runs
`ntl::net::borrowed_transform_pipeline` directly with caller-owned output storage and
validates the versioned explicit-offload contract.

The IOCTL is a deterministic contract-test transport, not a network example.
For real network classification and kernel-to-user policy delivery, continue
with [`examples/wfp/user/tcp-content-filter`](../../../examples/wfp/user/tcp-content-filter/)
and [`examples/wfp/user/udp-content-filter`](../../../examples/wfp/user/udp-content-filter/).
Those examples
own WFP lifetime, backpressure, timeout, and fail-closed behavior.

This project lives under `test/net` so its synthetic IOCTL transport cannot be
mistaken for an example that consumes real WFP traffic.

## Stack-safe workspaces

Large parser scratch and output records do not live on the kernel stack. The
driver owns an `ntl::net::kernel::workspace_pool`, and each IOCTL acquires a
small move-only lease:

```cpp
auto acquired = workspaces.try_acquire();
if (!acquired)
  return acquired.status();

auto workspace = std::move(*acquired);
auto status = inspect(input, workspace->reply, workspace->scratch);
```

The workspace is backed by a nonpaged lookaside list, is reconstructed for
each lease, and is returned automatically. QPACK and TLS reuse the same
bounded scratch storage. Protocol handlers are separate functions so Debug
builds do not combine every branch's locals into one kernel stack frame. The
application-facing IOCTL remains unchanged; callers never invoke a native pool
or stack-expansion API.

## Build and run

```bat
cmake -S test\net\kernel-contracts -B test\net\kernel-contracts\build_x64 -A x64
cmake --build test\net\kernel-contracts\build_x64 --config Debug
sc create CrtSysNtlNetKernelContracts binpath= "C:\path\to\crtsys_ntl_net_kernel_contract_driver.sys" type= kernel start= demand
sc start CrtSysNtlNetKernelContracts
test\net\kernel-contracts\build_x64\Debug\crtsys_ntl_net_kernel_contract_app.exe
sc stop CrtSysNtlNetKernelContracts
sc delete CrtSysNtlNetKernelContracts
```

Expected output:

```text
kernel network core ok: HTTP/1=40 HTTP/2=3 HTTP/3=3 gRPC=3 WebSocket=2 WebTransport=2 QPACK=1 TLS=2 transformed=32 offload=1 executor=1
```

For repeated load, execution, unload, and System-log crash checks in a test VM:

```powershell
.\Run-NtlNetKernelContracts.ps1 `
  -DriverPath .\build_x64\Debug\crtsys_ntl_net_kernel_contract_driver.sys `
  -AppPath .\build_x64\Debug\crtsys_ntl_net_kernel_contract_app.exe `
  -CrashPostcheckPath ..\..\common\Test-VmCrashPostcheck.ps1 `
  -AllowDisposableGuestMutation `
  -Cycles 20 -IterationsPerCycle 10 `
  -EvidencePath .\ntl-net-kernel-contract-evidence.json
```

Run this only in a disposable driver-test VM with the repository's normal
test-signing setup. The explicit mutation switch is accepted only when
`C:\crtsys-disposable-test-guest.sentinel` exists and contains exactly
`CRTSYS_DISPOSABLE_TEST_GUEST`; the runner never creates the sentinel. The
script never reboots the machine or changes Verifier.
It records the current System-event RecordId and crash-dump fingerprints before
loading the driver, then rejects only new crash evidence. Copy
`test/common/Test-VmCrashPostcheck.ps1` and
`test/wfp/runtime/common/DisposableGuestGuard.ps1` beside the runner when
packaging it for a guest; in a source checkout the runner finds both common
scripts automatically.
After you configure Driver Verifier for
`crtsys_ntl_net_kernel_contract_driver.sys` and reboot
the VM manually, add `-RequireVerifierTarget`; the suite then fails before
loading the driver if the target is not active. Use
`-CaptureVerifierSettings` when settings should be recorded without requiring
this driver as a target. Verifier queries and every launched process have a
bounded timeout (`-OperationTimeoutSeconds`, 30 seconds by default).

While the suite is running it rewrites
`ntl-net-kernel-contract-evidence.json.progress.json` with the current phase, cycle,
iteration, and completed-run count. The final evidence remains a separate
PASS/FAIL JSON file, so a guest-control timeout can be distinguished from a
driver, app, service-control, or Verifier timeout.

The app executes every bounded driver handler: HTTP/1, HTTP/2, and HTTP/3
framing, gRPC framing, WebSocket unmasking, WebTransport stream-prefix parsing,
static QPACK, TLS ClientHello/SNI/ALPN observation, direct content
transformation, and explicit-offload request/response validation. A nonzero
exit code identifies the failed stage.
The final request posts work through `ntl::net::kernel::executor`, waits for
its completion, and unload drains the executor after it stops accepting work.

The aggregate public kernel header is `<ntl/net/kernel/all>`. This contract
currently
tests the shared parsing and policy core, direct kernel execution, explicit
offload validation, kernel Schannel credential acquisition, and official-NMR
kernel MsQuic binding. The lifetime stages release the last Schannel
credential, completed coroutine frame, WSK/TLS transport chain, MsQuic
configuration chain, and workspace lease at `DISPATCH_LEVEL`; success requires
their runtime-owned cleanup to finish at `PASSIVE_LEVEL`. Explicit close and
drain calls test deterministic service shutdown, not a destruction-order
requirement. No user-mode `msquic.dll` fallback is used.
