# crtsys NuGet Consumer Test

This directory contains a Visual Studio WDK driver project used by CI to verify
the native NuGet package.

The test installs the generated `crtsys` package into a copy of this project
and builds it with MSBuild. The project intentionally relies on
`build/native/crtsys.props` and `build/native/crtsys.targets` from the package
for `crtsys.lib`, `Ldk.lib`, include paths, forced includes, and the
`CrtSysDriverEntry` entry point.

It compiles the same driver test sources used by the CMake test driver, with a
small local Google Test compatibility header so the test sources can build
without bringing CMake dependencies into the native NuGet consumer project.
