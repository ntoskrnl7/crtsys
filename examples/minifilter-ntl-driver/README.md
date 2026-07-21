# NTL Minifilter Driver Sample

This sample shows the core shape of an NTL file-system minifilter without
mixing in stress or multi-instance verification. It registers typed create,
read, write, and cleanup callbacks. Post-create captures normalized name
information in a typed stream context, and later I/O callbacks reuse that
snapshot instead of issuing another name query on an unsafe path.

The same driver also exposes a typed Filter Manager communication port. The
app and driver share one versioned `NTL_FLT_RPC_BEGIN_CONTRACT` declaration.
The generated API performs a synchronous call, an asynchronous/coroutine call,
receives a typed driver notification, and opens a bidirectional typed stream.
The app writes `21`, the minifilter queues `42`, and the app acknowledges that
reliable stream record. It then pins one shared region containing a pair of
`ntl::ipc::shared_ring` queues, sends pointer-free ring tokens to the driver,
and receives transformed records without serializing each record through
`FilterSendMessage`. See
[`shared/minifilter_sample.hpp`](shared/minifilter_sample.hpp) for the contract
and handlers, [`driver/communication.cpp`](driver/communication.cpp) for
server attachment, [`app/communication.cpp`](app/communication.cpp) for the
sync/async client flow, and
[`app/coroutine_call.cpp`](app/coroutine_call.cpp) for `co_await`.

Post-create uses the flags-less `on()` form. That signature deliberately means
normal-completion-only: NTL does not invoke it during instance draining. Use
`on_with_completion<T>()` when pre-operation owns state that must be released
after normal completion or during teardown; see the
[minifilter guide](../../docs/ntl/minifilter.md).

The app first verifies the typed port and shared-ring path, then creates,
writes, reads, renames, and deletes one temporary `.tmp` file. The same app can
be built as x86 and connected to the x64 driver because the wire header and
shared-memory tokens contain fixed-width fields rather than native pointers.
The INF defines one automatically attached development instance.

## CMake

```bat
cmake -S examples\minifilter-ntl-driver -B examples\minifilter-ntl-driver\build_x64 -A x64
cmake --build examples\minifilter-ntl-driver\build_x64 --config Debug
```

To build only the x86 client for cross-bitness use with an x64 minifilter:

```bat
cmake -S examples\minifilter-ntl-driver -B examples\minifilter-ntl-driver\build_app_x86 -A Win32
cmake --build examples\minifilter-ntl-driver\build_app_x86 --config Release --target crtsys_minifilter_ntl_sample_app
```

The CMake build consumes the current crtsys source tree, including the NTL
headers in this checkout. The Visual Studio solution consumes
`artifacts/development-package` when that local package exists; otherwise it
restores the latest published `crtsys` NuGet package. Refresh the development
package before using the solution to validate uncommitted runtime or NTL
changes.

The minifilter entry model and Filter Manager library are selected explicitly:

```cmake
crtsys_add_driver(my_filter MINIFILTER NTL driver/main.cpp)
```

`MINIFILTER NTL` selects `ntl::flt::main`, links `fltmgr.lib`, and retains the
standard Filter Manager registration and unload lifecycle. Use `MINIFILTER`
without `NTL` to keep an ordinary `DriverEntry` while still linking Filter
Manager.

## Install And Run

Install only in an isolated test VM. Copy the `.sys` beside the INF, then run:

```bat
pnputil /add-driver crtsys_minifilter_ntl_sample.inf /install
fltmc load CrtSysMinifilterNtlSample
crtsys_minifilter_ntl_sample_app.exe
fltmc unload CrtSysMinifilterNtlSample
```

The Visual Studio project leaves signing disabled so it does not assume a
developer certificate. Sign the driver with a test certificate trusted by the
VM before installation.

The INF selects the legacy `Instances` layout before Windows 11 24H2 and the
isolated `Parameters\Instances` layout on build 26100 and later. Its altitude
is for local sample testing only. A distributed minifilter must use an altitude
allocated according to Microsoft's minifilter altitude policy.

The comprehensive multi-instance, context-lifetime, WhenSafe, detach,
cross-bitness, concurrent async/cancellation, malformed-message, and
stale-token coverage lives in
[`test/flt/runtime`](../../test/flt/runtime).
