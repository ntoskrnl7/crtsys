# NTL Minifilter I/O Buffer Runtime Test

The `crtsys_flt_io_buffer_runtime_test` target is the end-to-end fixture for
`ntl::flt::swapped_io_buffers`.

It installs an isolated replacement in pre-write, maps that replacement into
the process connected to its Filter Manager communication port, and asks the
user app to XOR-transform it without modifying the application's original
buffer. Pre-read installs a replacement output; post-read maps only the
`IoStatus.Information` range to the same service, then closes the mapping and
copies the transformed bytes back. Target IRP pre work and successful read
post work are deliberately pended to PASSIVE workers on every request, so a
passing run proves both deferred completion paths. The write worker transfers
the operation into `pending_pre_operation_queue` and resumes it only after the
service closes the mapping; the read worker transfers post-read ownership into
`pending_post_operation_registry` and replies only after the same close. Thus
successful file I/O also proves the normal pending-registry resume/reply
paths. Each detached service worker owns a separate strong instance-context
reference and runs through `queue_instance_work_item`; Filter Manager therefore
holds filter rundown until the worker routine actually returns. Instance
teardown can cancel either registry without invalidating late worker state or
unloading its callback code. Fast I/O is disallowed and retried on the IRP path
when emitted.

The app proves both sides of the transformation:

1. With the filter loaded, a write followed by a read returns the original
   plaintext.
2. With the filter unloaded, the same file contains the exact XOR ciphertext.
3. After reload, reading the ciphertext returns the original plaintext again.
4. A nonzero-length read at EOF succeeds with zero transferred bytes, proving
   that zero-byte copy-back does not dereference the original destination.
5. The mapped address is no longer a valid VAD after I/O completion, and an
   operation attempted after the service disconnects fails instead of using a
   stale process or silently bypassing transformation.
6. A user handler that withholds its reply beyond the service timeout cancels
   both a pended pre-WRITE and pended post-READ. Their VADs are invalid before
   the deliberately late handler is released. The canceled WRITE uses a
   distinct payload and must not change the lower file.
7. Disconnecting the communication client while each direction is pended
   cancels the I/O, drains its pending registry, invalidates its VAD, and
   permits a clean service reconnect. Its canceled WRITE likewise must not
   change the lower file.
8. Unloading the filter while each direction is pended cancels the I/O,
   invalidates its VAD, waits for Filter Manager work-item rundown, and permits
   a subsequent load/read cycle.

Build it as part of the existing minifilter runtime project:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-runtime -Architecture x64 `
  -PlatformToolset v145 -Configuration Debug
```

Install and test only on a disposable, test-signing-enabled VM. The build
copies `crtsys_flt_io_buffer_runtime_test.inf` beside the driver output; create
and trust a test certificate, generate/sign the catalog package according to
the WDK test-signing workflow, and install that staged INF. Enable Driver
Verifier for `crtsys_flt_io_buffer_runtime_test.sys`, reboot if Verifier
requests it, and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Run-FltIoBufferRuntimeTests.ps1 `
  -AppPath test\flt\runtime\build_x64_v145\Debug\crtsys_flt_io_buffer_runtime_test_app.exe `
  -RequireVerifier
```

The runner verifies elevation, test signing, the installed service, and the
active Driver Verifier target before starting. The app unloads and reloads the
installed filter and fails on the first plaintext, ciphertext, copy-back,
timeout, disconnect, teardown, VAD-lifetime, load, or unload mismatch. The
runner also verifies that the app did not leave the test filter loaded.
