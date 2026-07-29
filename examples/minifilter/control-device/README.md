# NTL minifilter control-device sample

[한국어 설명](./README.ko-KR.md)

This sample implements the reusable part of the WDK `cdo` sample with typed
NTL ownership. It shows a legacy control device beside a minifilter without
putting raw WDM dispatch or `fltKernel.h` in application driver code.

| WDK mechanism | NTL expression |
| --- | --- |
| `IoCreateDevice` and symbolic link | `driver.add_control_device<T>()` |
| `DriverObject->MajorFunction[]` | typed `device.on_*()` handlers |
| manual `METHOD_BUFFERED` validation | `ioctl_from_contract` helpers |
| unload refusal with open references | typed `on_unload(unload_flags)` |
| device/link deletion | automatic `ntl::flt::driver` teardown |

## Flow

1. `ntl::flt::main` describes the control device before filtering starts.
2. NTL creates and configures the device, then publishes its DOS name.
3. The app opens `\\.\CrtSysMinifilterControlDevice` and sends a typed ping.
4. An optional `FilterUnload` is refused while the handle is open.
5. Dispatch remains usable after the veto; closing the handle permits unload.

The driver source starts with only `#include <ntl/flt/all>`. Native Filter
Manager and WDM entry/dispatch tables stay in NTL.

## Build and run

```powershell
cmake -S examples\minifilter\control-device `
      -B out\minifilter-control-device-x64 -A x64
cmake --build out\minifilter-control-device-x64 --config Debug
```

For a direct Visual Studio/WDK build, open
`crtsys_minifilter_control_device_sample_vs.sln`, or run:

```powershell
msbuild crtsys_minifilter_control_device_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

After installing the test-signed driver in a disposable VM:

```powershell
fltmc load CrtSysMinifilterControlDeviceSample
crtsys_minifilter_control_device_sample_app.exe
fltmc unload CrtSysMinifilterControlDeviceSample
```

The app must run elevated because it deliberately attempts `FilterUnload`.
The development altitude `370030.130` is not suitable for distribution.

The exhaustive x64/WOW64 and Driver Verifier fixture remains under
[`test/flt/runtime/cdo_*`](../../../test/flt/runtime).
