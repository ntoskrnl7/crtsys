# CDO Minifilter Runtime Fixture

This driver/app pair verifies `ntl::flt::driver::add_control_device()`. The
driver source includes only `<ntl/flt/all>` at its public NTL boundary; it does
not include `fltKernel.h`, assign a raw WDM major-function table, create a
native device object, or delete one during unload.

The minifilter queues a named `ntl::device<cdo_extension>` before
`driver.start()`. NTL creates it between filter registration and filtering
startup, configures typed create/cleanup/close/device-control handlers, and
then publishes `\\DosDevices\\CrtSysFltCdoRuntime`.

The VM app verifies:

- `\\.\CrtSysFltCdoRuntime` is user-openable;
- a second concurrent create receives the driver's single-open failure;
- a shared `ntl::ioctl_from_contract` ping validates typed input and output;
- `FilterUnload` reaches the minifilter and is vetoed while the CDO has an
  open reference;
- the open handle still dispatches IOCTLs after that veto;
- cleanup and close permit a later open;
- the runner's final unload removes the link and device and unloads the
  minifilter.

The application success contract is:

```text
cdo_integration=PASS concurrent_open_error=548
unload_veto=0x801F0010 creates=2 ioctls=3
```

Validate this scenario with Standard Driver Verifier flags `0x1209BB` and with
both x64 and WOW64 applications against the x64 driver. Preserve the resulting
logs in the test environment's configured artifact destination.
