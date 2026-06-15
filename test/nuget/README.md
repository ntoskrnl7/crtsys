# crtsys NuGet Consumer Test

This directory contains Visual Studio/MSBuild consumer projects used by CI to
verify the native NuGet package.

The test script installs the generated `crtsys` package into a copied test
tree and builds the selected project with MSBuild.

- `crtsys_nuget_app_test.vcxproj` builds the same user-mode app test source as
  the CMake app test on x86, x64, and ARM64. It verifies that package headers
  can be consumed without enabling driver link settings.
- `crtsys_nuget_test.vcxproj` builds the same WDK driver test sources as the
  CMake driver test on x64 and ARM64. It relies on package props/targets for
  `crtsys.lib`, `Ldk.lib`, include paths, forced includes, and the
  `CrtSysDriverEntry` entry point.

Both projects use a small local Google Test compatibility header. The driver
test keeps the CMake driver's `nlohmann_json.cpp` coverage by installing the
official `nlohmann.json` NuGet package and importing its native MSBuild target.
