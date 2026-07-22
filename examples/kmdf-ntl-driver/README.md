# NTL KMDF Driver Sample

This sample demonstrates an NTL-style KMDF driver using the MSVC STL through
`crtsys`. KMDF continues to own the WDF driver, device, queue, request, PnP,
power, and object-lifetime model. `crtsys` supplies the kernel-compatible CRT
and STL startup and shutdown path plus thin C++ facades under `ntl/kmdf/`.

The driver is a non-PnP KMDF control driver so it can be loaded and unloaded as
a service in an isolated test VM. Its parallel default queue is explicitly
configured for `WdfExecutionLevelPassive`; this lets a release IOCTL complete
an earlier request waiting in the manually dispatched queue. The IOCTL callback
uses `std::vector`, `std::accumulate`, and `std::format`, catches all C++
exceptions at the WDF callback boundary, and returns the observed server IRQL
to the app.
The same open/IOCTL/close flow constructs and destroys non-trivial
`device_state` and per-open `file_state` objects in WDF-owned context storage.
Typed file callbacks also demonstrate the `ntl::kmdf::file::wdm()` bridge to a
non-owning `ntl::file` view of the native `FILE_OBJECT`. Driver setup also
creates a parented KMDF work item and passive timer: the work item is flushed
to prove PASSIVE_LEVEL execution, while the timer exercises WDF-owned deferred
callback lifetime.
During device setup the sample also allocates parented `WDFMEMORY`, verifies
buffer copies, creates a general I/O target, and creates then automatically
deletes an unsent `ntl::kmdf::driver_request`.
It also exercises the common WDF object utilities in one runtime path:
`spin_lock`, `wait_lock`, move-owned lookaside memory, `collection`, `string`,
and a standalone `dpc`. The DPC callback performs only lock-free counter and
event operations at `DISPATCH_LEVEL`; the passive setup path waits for and
checks the result.

The app also exercises a manually dispatched queue with real overlapped I/O.
One pending IOCTL is retrieved by its KMDF file object and completed normally;
a second is canceled with `CancelIoEx` while still queued. The driver verifies
the move-only request ownership transition and completes the framework's
`EvtIoCanceledOnQueue` callback exactly once.

The default queue also assigns one KMDF forward-progress reserved request.
The one-time allocation callback receives the reserved-only
`reserved_request_resources` view before the device starts. Its
`request_resources` callback also records every ordinary request before queue
insertion, and the app verifies that the counter advances. Neither restricted
view can complete or forward the request, and their distinct types prevent
ordinary I/O from being mistaken for reserved-request fallback traffic.

This sample intentionally remains a non-PnP control device. For typed child
enumeration and PDO creation, use the separate
[KMDF bus and PDO sample](../kmdf-bus-ntl-driver).

## Visual Studio and NuGet

Open `crtsys_kmdf_ntl_sample_vs.sln`, restore packages, and build `Debug|x64`
or `Release|x64`. In **Project Properties > Driver Settings > Driver Model**,
set **Type of driver = KMDF**, then choose **NTL KMDF** under
**crtsys KMDF entry point**. The package stores the crtsys selection as:

```xml
<KmdfVersion>1.15</KmdfVersion>
<CrtSysDriverModel>NtlKmdf</CrtSysDriverModel>
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

The dropdown selection enables `ntl::kmdf::main`. Choose **No NTL entry point**
when you need the ordinary KMDF entry model.

## CMake

```bat
cmake -S examples\kmdf-ntl-driver -B examples\kmdf-ntl-driver\build_x64 -A x64
cmake --build examples\kmdf-ntl-driver\build_x64 --config Debug
```

The KMDF version is selected on the existing driver helper:

```cmake
crtsys_add_driver(my_driver KMDF 1.15 NTL driver/main.cpp)
```

Omit `NTL` to use standard KMDF `DriverEntry`. WDM remains the default when
`KMDF` is omitted.

## VM Smoke Test

```bat
sc create CrtSysKmdfNtlSample binpath= "C:\path\crtsys_kmdf_ntl_sample.sys" type= kernel start= demand
sc start CrtSysKmdfNtlSample
crtsys_kmdf_ntl_sample_app.exe 36
sc stop CrtSysKmdfNtlSample
sc delete CrtSysKmdfNtlSample
```

Expected app output:

```text
NTL KMDF ok: value=36 result=42 server_irql=0 message=KMDF transformed 36 to 42
NTL KMDF manual queue ok: released=1 canceled=1
```
