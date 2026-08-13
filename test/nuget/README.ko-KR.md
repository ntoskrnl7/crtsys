# crtsys NuGet 소비자 테스트

이 디렉터리에는 네이티브 NuGet 패키지를 검증하기 위해 CI에서 사용하는 Visual Studio/MSBuild 소비자 프로젝트가 있습니다.

테스트 스크립트는 생성된 `crtsys` 패키지를 복사한 테스트 트리에 설치하고, 선택한 프로젝트를 MSBuild로 빌드합니다. 명시적인 `CrtSysPackageRoot` 없이 프로젝트를 Visual Studio에서 직접 열면 `PackageReference`로 최신 안정 버전을 복원합니다. 그러면 include 경로, 강제 include, IntelliSense, 링크 설정이 일반적인 방식으로 구성됩니다.

이 프로젝트들은 공개 [MSBuild/NuGet 빠른 시작](../../docs/msbuild-nuget-quickstart.ko-KR.md)의 스모크 테스트에 해당합니다. 패키지 사용은 Visual Studio뿐 아니라 `msbuild /restore`를 실행할 수 있는 Build Tools 전용 환경에서도 동작해야 합니다.

- `crtsys_nuget_app_test.vcxproj`는 x86, x64, ARM64의 Debug/Release 구성에서 CMake 앱 테스트와 같은 사용자 모드 앱 테스트 소스를 빌드합니다. 드라이버 링크 설정을 켜지 않고도 패키지 헤더를 사용할 수 있는지 검증합니다.
- `crtsys_nuget_test.vcxproj`는 x86, x64, ARM64의 Debug/Release 구성에서 CMake 드라이버 테스트와 같은 WDK 드라이버 테스트 소스를 빌드합니다. `crtsys.lib`, `Ldk.lib`, include 경로, 강제 include, `CrtSysDriverEntry` 진입점은 패키지 props/targets가 제공합니다.

두 프로젝트는 작은 로컬 Google Test 호환 헤더를 사용합니다. 드라이버 테스트는 공식 `nlohmann.json` NuGet 패키지를 설치하고 네이티브 MSBuild 대상을 가져와 CMake 드라이버의 `nlohmann_json.cpp` 검사 범위를 유지합니다. 이 스모크 테스트에서는 드라이버 서명을 비활성화하며, 런타임 서명과 로드 테스트는 별도의 CMake 드라이버 CI 경로에서 수행합니다.
