# CI 드라이버 로드 테스트

`CMake` 워크플로는 두 계층으로 구성됩니다.

1. GitHub 호스팅 러너가 테스트 앱과 선택한 각 도구 집합이 지원하는 아키텍처의
   드라이버 대상을 빌드하고, 드라이버를 테스트 서명한 뒤 아티팩트를 업로드합니다.
2. `workflow_dispatch`를 `run_driver_load_tests=true`로 실행하면, 준비된 자체 호스팅
   Windows 러너가 그 아티팩트를 내려받아 서명된 드라이버를 로드하고 x64 테스트 앱을
   실행합니다.

이 분리는 GitHub 호스팅 러너에서는 재현 가능한 빌드를 유지하고, 커널 드라이버 로드는
명시적으로 준비한 장비에서만 수행하게 합니다.

## 러너 요구 사항

다음을 갖춘 Windows x64 장비 또는 VM을 준비하세요.

- 이 저장소에 등록된 GitHub Actions 자체 호스팅 러너
- 러너 레이블: `self-hosted`, `windows`, `x64`, `crtsys-driver-test`
- 승격되어 실행되거나 커널 드라이버 서비스를 만들고 시작할 수 있는 서비스 계정으로
  설치된 러너 프로세스
- 부팅 전에 활성화한 테스트 서명
- 테스트 서명 활성화를 방해하는 경우 비활성화한 Secure Boot

자체 호스팅 러너는 로드 테스트 작업을 위해 Visual Studio, CMake, WDK를 설치할 필요가
없습니다. 워크플로가 같은 실행에서 이미 빌드한 아티팩트를 내려받습니다.

커널 드라이버는 테스트 장비의 커널 아키텍처와 일치해야 하므로 로드 테스트 작업은
x64입니다. ARM 또는 ARM64 드라이버를 시험하려면 그에 맞는 Windows ARM 또는 ARM64
자체 호스팅 러너와 테스트 앱 파이프라인이 필요합니다. x86 드라이버는 32비트 Windows
테스트 장비가 필요하며 x86 커널 드라이버는 x64 Windows에 로드할 수 없습니다.

## 테스트 서명 활성화

러너 장비의 관리자 권한 PowerShell 프롬프트에서 실행합니다.

```powershell
bcdedit /set testsigning on
```

장비를 재부팅한 다음 확인합니다.

```powershell
bcdedit /enum '{current}'
```

출력에는 `testsigning Yes` 또는 `testsigning on`이 있어야 합니다.

## 러너 등록

GitHub에서 새 자체 호스팅 러너를 만듭니다.

```text
Repository -> Settings -> Actions -> Runners -> New self-hosted runner
```

Windows용으로 GitHub가 제공하는 명령을 사용하세요. 러너 구성 시 사용자 지정 레이블을
추가합니다.

```powershell
.\config.cmd --url https://github.com/ntoskrnl7/crtsys --token <token> --labels crtsys-driver-test
```

재부팅 뒤에도 유지하려면 서비스로 설치합니다.

```powershell
.\svc install
.\svc start
```

로드 테스트를 시작하기 전에 GitHub에서 러너가 온라인으로 보이는지 확인하세요.

## 로드 테스트 실행

GitHub Actions에서 `CMake` 워크플로를 열고 `Run workflow`를 선택한 뒤 다음을 설정합니다.

```text
run_driver_load_tests = true
```

워크플로는 다음을 수행합니다.

- 필수 레이블을 가진 온라인 러너를 사전 점검합니다.
- `crtsys-test-driver-x64`를 빌드하고 업로드합니다.
- 선택한 워크플로 매트릭스에 해당 도구 집합/아키텍처 조합이 있으면
  `crtsys-test-driver-ARM`을 빌드하고 업로드합니다.
- `crtsys-test-driver-ARM64`를 빌드하고 업로드합니다.
- `crtsys-test-app-x64`를 빌드하고 업로드합니다.
- 자체 호스팅 러너에서 해당 아티팩트를 내려받습니다.
- 임시 `CrtSysTest` 커널 서비스를 만듭니다.
- 드라이버를 시작하고 `crtsys_test_app.exe`를 실행한 뒤 서비스를 중지·삭제합니다.

드라이버를 로드하기 전에 테스트가 실패한다면 러너가 승격되었는지와
`bcdedit /enum '{current}'`가 테스트 서명 활성화를 보고하는지를 확인하세요.
