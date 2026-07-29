# NTL KMDF echo and cancellation sample

[한국어 설명](./README.ko-KR.md)

This root-enumerated PnP driver keeps one delayed echo request in a
sequential KMDF queue. A one-shot WDF timer completes the request, while the
request's cancellation callback handles `CancelIoEx`.

The queue uses `WdfSynchronizationScopeQueue`, and the timer is parented to
that queue with automatic serialization. Queue dispatch, timer expiry, and
request cancellation therefore share the framework queue lock. The source
still performs the required `try_mark_cancelable()` /
`try_unmark_cancelable()` handshake: if cancellation wins,
`try_unmark_cancelable()` returns `STATUS_CANCELLED` and only the cancel
callback completes the request.

The application makes three observable assertions:

1. a short request completes from the timer;
2. a long request is canceled and reports `ERROR_OPERATION_ABORTED`; and
3. a later request proves that the queue recovered and reports both the
   completion and cancellation counters.

## Build

Open `crtsys_kmdf_echo_ntl_sample_vs.sln`, or build with CMake:

```powershell
cmake -S examples\kmdf\echo `
      -B artifacts\examples\kmdf-echo -A x64
cmake --build artifacts\examples\kmdf-echo --config Debug
```

## Disposable VM smoke test

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_echo_ntl_sample_app.exe
.\remove.ps1
```

The expected application line begins with `NTL KMDF echo ok`. On a fresh
device it reports two completed requests and one canceled request; repeated
runs verify that the cumulative counters advance by one completion and one
cancellation after the first echo. Use only a disposable test VM when
installing development drivers.
