# MSBuild/NuGet 빠른 시작

[README로 돌아가기](./ko-kr.md)

이 경로는 `crtsys`를 native NuGet package로 소비하는 Visual Studio 또는
Build Tools WDK driver project용입니다. CMake/CPM GitHub 소비 경로와는
독립적인 사용 방식입니다.

## 요구 사항

- Visual Studio 또는 Build Tools 2017 이상
- 선택한 toolset에 맞는 Windows SDK와 WDK
- NuGet restore를 지원하는 MSBuild
- `crtsys` package가 있는 NuGet source 접근 권한

modern `PackageReference` project에서는 `nuget.exe`가 필수는 아닙니다.
MSBuild restore가 가능하면 `msbuild /restore`로 충분합니다. `nuget.exe`는
구형 `packages.config` 흐름이나 script가 `nuget restore`를 직접 호출하는
경우에만 별도로 설치하면 됩니다.

## Visual Studio

Package Manager Console에서:

```powershell
Install-Package crtsys
```

그 다음 WDK driver project를 `x64` 또는 `ARM64`로 일반적인 방식대로 빌드합니다.

## Build Tools only

driver project에 `PackageReference`를 추가합니다.

```xml
<ItemGroup>
  <PackageReference Include="crtsys" Version="<version>" />
</ItemGroup>
```

MSBuild, SDK, WDK가 잡힌 Developer PowerShell 또는 Developer Command Prompt를
열고 restore와 build를 함께 실행합니다.

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=x64
```

`ARM64`는 다음처럼 빌드합니다.

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Release /p:Platform=ARM64
```

## Package가 가져오는 것

native package는 WDK consumer project에 필요한 MSBuild props/targets를
제공합니다. 여기에는 include path, forced include, runtime library, LDK
library, package consumer test에서 쓰는 `CrtSysDriverEntry` entry point wiring이
포함됩니다.

driver는 여전히 일반 WDK driver입니다. Verifier, signing, target OS policy,
IRQL, paging, unload safety는 driver project가 책임집니다.

## CI smoke test 구조

이 저장소는 NuGet 소비가 문서로만 존재하지 않도록
[`test/nuget`](../test/nuget)에 consumer project를 둡니다.

- `crtsys_nuget_app_test.vcxproj`는 user-mode header/package 소비를 확인합니다.
- `crtsys_nuget_test.vcxproj`는 package로부터 WDK driver test source를
  `x64`와 `ARM64` `Debug`/`Release`에서 빌드합니다.

CI job도 같은 모양으로 실행할 수 있습니다.

```powershell
msbuild .\test\nuget\crtsys_nuget_test.vcxproj /restore /p:Configuration=Release /p:Platform=x64
```

runtime driver load는 package consumption과 별도 문제입니다. 이 경로는
[CI driver load tests](./ci-driver-load-tests.md)에 정리되어 있습니다.
