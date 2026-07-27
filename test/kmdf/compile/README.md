# KMDF compile contracts

This target compiles representative `ntl::kmdf` contracts without requiring a
device or loading a driver. It checks move-only request and interface
ownership, callback signatures, handle-sized facades, queue/deferred
callbacks, PnP and filter forwarding, child/PDO, interrupt, DMA, USB, and WMI
construction paths.

Build both supported client/driver architectures through the CI entry point:

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-compile `
  -Architecture x64 `
  -Configuration Debug

.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-compile `
  -Architecture x86 `
  -Configuration Debug
```

The target uses `/W4 /WX`. It complements rather than replaces the public
examples and loaded-driver VM tests.
