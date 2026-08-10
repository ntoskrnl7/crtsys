# crtsys vcpkg 포트

이 디렉터리는 crtsys 사전 빌드 릴리스 번들을 설치하는 프로젝트 제공 overlay port를
포함합니다. 선택한 triplet의 아키텍처만 설치하고 지원되는 MSVC 툴셋 변형을
유지하며, 다음 두 사용 경로를 함께 제공합니다.

- `find_package(crtsys CONFIG REQUIRED)` 및 `crtsys_add_driver(...)`를
  사용하는 CMake
- 기존 crtsys 진입점 속성 페이지를 사용하는 Visual Studio/MSBuild

이 패키지는 Windows/WDK 전용 정적 라이브러리이며 정적 MSVC 런타임을
사용합니다.

## Git registry

게시된 버전은 소스 checkout 없이 사용할 수 있습니다. 소비자 manifest 옆에
다음 `vcpkg-configuration.json`을 추가합니다.

```json
{
  "default-registry": null,
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "6e1c3ad29a817831bcbf1eff9cfbfdaf487d35c7",
      "packages": ["crtsys"]
    }
  ]
}
```

이 최소 예제는 기본 registry를 비활성화합니다. 다른 vcpkg 의존성도 사용하는
프로젝트라면 기존에 고정한 default registry 설정을 유지하세요. `vcpkg.json`에는
다음 의존성을 추가합니다.

```json
{
  "name": "my-driver",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
```

그런 다음 호환되는 정적 CRT triplet을 선택합니다.

```powershell
vcpkg install --triplet=x64-windows-static
```

## Overlay port

로컬 포트 개발 또는 소스 checkout에서는 overlay를 명시적으로 선택합니다.

```powershell
vcpkg install --triplet=x64-windows-static `
  --overlay-ports=D:\path\to\crtsys\vcpkg\ports
```

독립 설치형 vcpkg에서는 classic mode의
`vcpkg install crtsys:x64-windows-static --overlay-ports=...`도 사용할 수 있습니다.

## CMake

vcpkg toolchain으로 사용 프로젝트를 구성한 뒤 설치된 패키지를 사용합니다.

```cmake
find_package(crtsys CONFIG REQUIRED)

set(CRTSYS_NTL_MAIN ON)
crtsys_add_driver(my_driver src/main.cpp)
```

기존 모델별 호출도 그대로 지원됩니다.

```cmake
crtsys_add_driver(my_kmdf KMDF 1.15 NTL src/main.cpp)
crtsys_add_driver(my_filter MINIFILTER NTL src/main.cpp)
crtsys_add_driver(my_wfp WFP NTL src/main.cpp)
```

## Visual Studio/MSBuild UI

vcpkg의 MSBuild 통합은 설치된 include 및 library 경로를 연결하지만 개별
포트의 전용 속성 페이지까지 자동으로 import하지는 않습니다. 최초
`vcpkg install` 실행 후 `Directory.Build.targets` 또는 사용하는 `.vcxproj`에서
crtsys bridge를 가져옵니다.

```xml
<Project>
  <Import
    Project="$([MSBuild]::NormalizePath('$(VcpkgManifestRoot)', 'vcpkg_installed', '$(VcpkgTriplet)', 'share', 'crtsys', 'msbuild', 'crtsys-vcpkg.targets'))"
    Condition="Exists('$([MSBuild]::NormalizePath('$(VcpkgManifestRoot)', 'vcpkg_installed', '$(VcpkgTriplet)', 'share', 'crtsys', 'msbuild', 'crtsys-vcpkg.targets'))')" />
</Project>
```

`VcpkgTriplet`은 `x64-windows-static`과 같은 정적 CRT triplet으로 지정합니다.
최초 설치 후 Visual Studio 솔루션을 다시 열면 MSBuild가 새 import를 평가합니다.
그러면 기존 **No NTL entry point**, **NTL WDM**, **NTL KMDF**,
**NTL Minifilter**, **NTL WFP** 선택이 NuGet 패키지와 동일하게 동작합니다.

NuGet은 별도 import가 필요 없는 Visual Studio 설치 경로로 계속 유지됩니다.
vcpkg bridge는 의존성 복원을 vcpkg manifest로 통일하면서도 crtsys WDK 속성
UI가 필요한 저장소를 위한 경로입니다.

## 검증

릴리스를 다운로드하지 않는 빠른 계약 검사는 다음과 같습니다.

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -ContractOnly
```

overlay port 설치와 MSBuild UI 계약까지 확인하려면 다음을 실행합니다.

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 `
  -Triplet x64-windows-static
```

태그 기반 릴리스 워크플로는 릴리스 준비 중 overlay manifest 버전을
갱신합니다. 검증된 GitHub Release 산출물이 업로드되면 Package 워크플로가
SHA-512를 계산하고 포트와 versions DB를 `vcpkg-registry` 브랜치에 게시한 뒤,
`main`의 소스 overlay와 문서 baseline을 동기화합니다.

로컬 복구 또는 검증에는 내부 유지보수 명령을 직접 사용할 수도 있습니다.

```powershell
./scripts/vcpkg/Update-CrtSysVcpkgPort.ps1 `
  -Version <version> -ArchivePath <prebuilt-zip>

./scripts/vcpkg/Publish-CrtSysVcpkgRegistry.ps1 `
  -Version <version> `
  -ArchivePath <prebuilt-zip> `
  -SourcePortDirectory ./vcpkg/ports/crtsys `
  -RegistryDirectory <registry-worktree>
```

게시 스크립트는 기존 버전의 이력 재작성과 registry baseline의 하향 이동을
거부합니다.
