# crtsys NuGet 소비자 테스트

이 디렉터리에는 네이티브 NuGet package를 검증하기 위해 CI에서 사용하는 Visual Studio/MSBuild 소비자 프로젝트가 있습니다.

테스트 script는 생성된 `crtsys` package를 복사한 테스트 tree에 설치하고 선택한 project를 MSBuild로 빌드합니다. 명시적 `CrtSysPackageRoot` 없이 project를 Visual Studio에서 직접 열면 `PackageReference`로 최신 stable package를 복원합니다. 따라서 include path, forced include, IntelliSense, linking이 일반적인 방식으로 구성됩니다.

이 project들은 공개 [MSBuild/NuGet 빠른 시작](../../docs/msbuild-nuget-quickstart.ko-KR.md)의 smoke-test 대응물입니다. package 소비는 Visual Studio뿐 아니라 `msbuild /restore`를 실행할 수 있는 Build Tools 전용 환경에서도 동작해야 합니다.

- `crtsys_nuget_app_test.vcxproj`는 x86, x64, ARM64의 Debug/Release에서 CMake app test와 동일한 사용자 모드 app test source를 빌드합니다. driver link 설정을 활성화하지 않고 package header를 소비할 수 있는지 검증합니다.
- `crtsys_nuget_test.vcxproj`는 x86, x64, ARM64의 Debug/Release에서 CMake driver test와 동일한 WDK driver test source를 빌드합니다. `crtsys.lib`, `Ldk.lib`, include path, forced include, `CrtSysDriverEntry` entry point는 package props/targets에 의존합니다.

두 project는 작은 로컬 Google Test 호환 header를 사용합니다. driver test는 공식 `nlohmann.json` NuGet package를 설치하고 네이티브 MSBuild target을 import하여 CMake driver의 `nlohmann_json.cpp` 범위를 유지합니다. 이 smoke test에서는 driver signing을 비활성화하며, runtime signing과 load test는 별도의 CMake driver CI 경로에 속합니다.
