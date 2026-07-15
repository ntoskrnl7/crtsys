# NTL KMDF Driver Sample

This sample demonstrates an NTL-style KMDF driver using the MSVC STL through
`crtsys`. KMDF continues to own the WDF driver, device, queue, request, PnP,
power, and object-lifetime model. `crtsys` supplies the kernel-compatible CRT
and STL startup and shutdown path plus thin C++ facades under `ntl/kmdf/`.

The driver is a non-PnP KMDF control driver so it can be loaded and unloaded as
a service in an isolated test VM. Its sequential default queue is explicitly
configured for `WdfExecutionLevelPassive`. The IOCTL callback uses
`std::vector`, `std::accumulate`, and `std::format`, catches all C++ exceptions
at the WDF callback boundary, and returns the observed server IRQL to the app.
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

The app also exercises a manually dispatched queue with real overlapped I/O.
One pending IOCTL is retrieved by its KMDF file object and completed normally;
a second is canceled with `CancelIoEx` while still queued. The driver verifies
the move-only request ownership transition and completes the framework's
`EvtIoCanceledOnQueue` callback exactly once.

## Visual Studio and NuGet

Open `crtsys_kmdf_ntl_sample_vs.sln`, restore packages, and build `Debug|x64`
or `Release|x64`. The driver project uses the ordinary WDK setting:

```xml
<DriverType>KMDF</DriverType>
<KmdfVersion>1.15</KmdfVersion>
<CrtSysUseNtlKmdfMain>true</CrtSysUseNtlKmdfMain>
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysUseNtlKmdfMain` selects `ntl::kmdf::main`. Omit that property to keep
the standard KMDF `DriverEntry` model.

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
