# crtsys vcpkg 포트

이 디렉터리는 crtsys의 1차 제공 overlay 포트입니다. 공식 포트는 고정된
소스 리비전을 선택한 Windows static-CRT triplet으로 직접 빌드하고,
라이브러리를 vcpkg 표준 `lib/manual-link` 위치에 설치합니다.

- CMake: `find_package(crtsys CONFIG REQUIRED)`와
  `crtsys_add_driver(...)`
- Visual Studio/MSBuild: crtsys 드라이버 모델 속성 페이지

Visual Studio C++ 워크로드와 호환되는 WDK가 필요합니다. 이 포트는 소비자를
구성하는 동안 의존성을 내려받지 않습니다.

## Git registry

소비자 `vcpkg.json` 옆에 다음 `vcpkg-configuration.json`을 둡니다.

```json
{
  "default-registry": null,
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "b125ae8c24005dd43ca003faf25f72d8ebf2297a",
      "packages": ["crtsys"]
    }
  ]
}
```

다른 vcpkg 패키지를 사용한다면 기존 default registry 설정은 유지해야
합니다. manifest에는 다음 의존성을 추가합니다.

```json
{
  "name": "my-driver",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
```

```powershell
vcpkg install --triplet=x64-windows-static
```

로컬 포트를 시험할 때는
`--overlay-ports=D:\path\to\crtsys\vcpkg\ports`를 추가합니다.

## 선택 기능

사용자 모드 zlib/Brotli 도우미는 명시적인 vcpkg 의존성입니다.

```json
"dependencies": [
  { "name": "crtsys", "features": ["content-codecs"] }
]
```

고정된 선택적 MsQuic 공개 헤더는 `msquic-headers` 기능을 사용합니다.
커널 모드 콘텐츠 codec은 vcpkg의 사용자 모드 codec 라이브러리로 빌드하지
않습니다. 별도로 검토한 커널 codec이 필요하면 소스에서 crtsys를 빌드하세요.

## CMake

```cmake
find_package(crtsys CONFIG REQUIRED)

set(CRTSYS_NTL_MAIN ON)
crtsys_add_driver(my_driver src/main.cpp)
```

`KMDF`, `MINIFILTER`, `WFP`, `NTL` 옵션도 그대로 사용할 수 있습니다.

## Visual Studio/MSBuild UI

manifest를 처음 설치한 뒤 manifest 루트에서 초기화 도구를 한 번 실행하고
솔루션을 다시 엽니다.

```powershell
vcpkg env --tools --triplet=x64-windows-static "crtsys-vs-init.cmd"
```

초기화 도구는 기존 `Directory.Build.props`와
`Directory.Build.targets`를 보존하며 반복 실행해도 안전합니다.
`crtsys-vs-init.cmd -Remove`로 제거할 수 있습니다. 포트에는
**No NTL entry point**, **NTL WDM**, **NTL KMDF**, **NTL Minifilter**,
**NTL WFP** 속성 페이지가 포함됩니다. NuGet은 초기화가 필요 없는
Visual Studio 설치 경로로 계속 제공됩니다.

## 검증과 게시

빠른 계약 검사는 다음과 같습니다.

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -ContractOnly
```

전체 검사는 소스 포트를 빌드하고 MSBuild UI도 검증합니다.

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -Triplet x64-windows-static
```

안정 태그를 게시한 뒤 태그 소스 archive로 포트 해시를 갱신합니다.

```powershell
./scripts/vcpkg/Update-CrtSysVcpkgPort.ps1 `
  -Version <version> -SourceArchivePath <source-tarball>
```

수동 **Update official vcpkg** Action은 안정 태그와 해시를 확인하고,
versions DB를 재생성하고, binary cache 없이 소스 포트를 빌드한 다음 CMake와
Visual Studio 소비자를 검사합니다. `validate`는 외부 상태를 바꾸지 않고,
`submit`은 fork 브랜치와 PR을 갱신합니다. 최종 병합에는
microsoft/vcpkg 리뷰와 CI 승인이 필요합니다.
