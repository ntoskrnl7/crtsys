# NTL minifilter communication sample

This sample isolates Filter Manager communication from file-I/O policy. The
driver registers an NTL typed communication port and the application exercises:

- synchronous and asynchronous RPC calls;
- a coroutine-based call wrapper;
- driver-to-application callbacks;
- best-effort and reliable notifications;
- typed streams; and
- two shared-memory rings exchanged through registered buffer tokens.

The minifilter registers no file-operation callbacks. Use the sibling
[`basic`](../basic) sample for callback and context fundamentals, and
[`swap-buffers`](../swap-buffers) for safe input/output buffer replacement.

## Build

From the repository root:

```powershell
cmake -S examples\minifilter\communication `
      -B out\minifilter-communication-x64 -A x64
cmake --build out\minifilter-communication-x64 --config Debug
```

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_communication_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_communication_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

The CMake target names are:

- `crtsys_minifilter_communication_sample`
- `crtsys_minifilter_communication_sample_app`

## Install and run

Test-sign the driver as required by the target machine, then install the INF:

```powershell
pnputil /add-driver `
  crtsys_minifilter_communication_sample.inf /install
fltmc load CrtSysMinifilterCommunicationSample
crtsys_minifilter_communication_sample_app.exe
fltmc unload CrtSysMinifilterCommunicationSample
```

The communication port is
`\CrtSysMinifilterCommunicationSamplePort`. The INF uses the sample altitude
`370030.128`; choose an altitude assigned for your real product before shipping.

See [the NTL minifilter guide](../../../docs/ntl/minifilter.md) for the API
model and [`test/flt/runtime`](../../../test/flt/runtime) for failure-path and
protocol coverage.
