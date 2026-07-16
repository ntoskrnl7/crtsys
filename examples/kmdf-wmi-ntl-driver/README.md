# NTL KMDF WMI sample

This PnP driver demonstrates the typed `ntl::kmdf` WMI surface end to end:

- a MOF-backed data provider with query, whole-instance set, set-item, and
  method callbacks;
- an event-only provider and a user-mode WMI event subscription;
- NTL-managed C++ contexts for the device and instances;
- a user-mode verifier that exercises `ROOT\\WMI` and the device interface.

Build `crtsys_kmdf_wmi_ntl_sample_vs.sln` in Visual Studio, or configure this
directory with CMake. The WDK MOF tools generate and validate the binary MOF
before it is embedded as the `CrtSysKmdfWmi` resource.

Install the root-enumerated test device from an elevated PowerShell session:

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
```

Then run `crtsys_kmdf_wmi_ntl_sample_app.exe`. It queries and updates the WMI
instance, invokes `Transform`, subscribes to the event class, triggers an
event through the sample IOCTL, and validates every returned value.

Remove the sample device with `remove.ps1`. Query, set, set-item, and method
callbacks run at `PASSIVE_LEVEL` by the native KMDF WMI contract, which permits
the sample's general CRT/STL work. WMI provider and instance objects must leave
`WDF_OBJECT_ATTRIBUTES::ExecutionLevel` at `WdfExecutionLevelInheritFromParent`;
WDF rejects an explicit execution level for those object types.
The event-only provider follows KMDF's single-instance path and is created
together with its instance from `wmi_provider_config`.
