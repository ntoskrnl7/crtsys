# NTL minifilter operation-log sample

[한국어 설명](./README.ko-KR.md)

This is the small, readable counterpart of the WDK **MiniSpy** sample. It keeps
the complete path visible in one example:

`typed I/O callback -> bounded nonpaged queue -> typed port RPC -> console`

| WDK MiniSpy responsibility | NTL sample form |
| --- | --- |
| `FLT_OPERATION_REGISTRATION` table | typed `registration::on*` calls |
| untyped completion context | `completion_slot<completion_state>` |
| per-open tracking | `stream_handle_context<tracked_handle>` |
| record list and synchronization | `record_queue` |
| command/reply protocol | typed `ntl::rpc::method` descriptors |
| Filter Manager port | `driver::add_communication_port` |

Only files ending in `.ntlspy` are logged, so normal system activity does not
bury the sample output. The app creates one such file, writes and reads it,
closes its handle, and drains the queue. It requires create/read/write/cleanup
and reports whether close was already observed. `IRP_MJ_CLEANUP` is issued when
the last handle is closed, while `IRP_MJ_CLOSE` is issued only after the last
reference to the file object is released, so a synchronous user-mode test
cannot require close to have arrived immediately after `CloseHandle`.

The queue is intentionally a separate file. It is ordinary kernel-side policy;
the minifilter-specific code in `driver/main.cpp` stays focused on typed
callbacks and ownership.

## Build

From a Visual Studio developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
```

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_operation_log_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_operation_log_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

Install and load the test-signed driver in a disposable VM, then run
`crtsys_minifilter_operation_log_sample_app.exe` as an administrator.

This example demonstrates the architecture, not every MiniSpy production
feature. The exhaustive runtime fixture also tests overflow, sequence ordering,
and unload-time queue draining under `test/flt/runtime`.
