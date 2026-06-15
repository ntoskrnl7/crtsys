# crtsys NuGet Consumer Test

This directory contains a Visual Studio WDK driver project used by CI to verify
the native NuGet package.

The test installs the generated `crtsys` package into a copy of this project
and builds it with MSBuild. The project intentionally relies on
`build/native/crtsys.props` and `build/native/crtsys.targets` from the package
for `crtsys.lib`, `Ldk.lib`, include paths, forced includes, and the
`CrtSysDriverEntry` entry point.

It compiles the same driver test sources used by the CMake test driver, with a
small local Google Test compatibility header. The CMake driver's
`nlohmann_json.cpp` coverage is kept by installing the official
`nlohmann.json` NuGet package and importing its native MSBuild target.
