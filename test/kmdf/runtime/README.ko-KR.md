# KMDF 소프트웨어 전용 런타임 모음

이 픽스처는 공개된 소프트웨어 전용 KMDF 예제를 하나의 결과 확인 가능한 VM 검증
단계로 실행합니다. 같은 드라이버의 중복 사본을 따로 유지하지 않고, 읽기 쉬운 기존
예제를 그대로 재사용합니다.

이 모음은 다음을 다룹니다.

- 비 PnP 제어 장치의 로드, I/O, 취소, 언로드
- 루트 PnP 설치, 장치 인터페이스 I/O, 재시작, 제거
- 지연된 echo 완료와 `CancelIoEx`
- 버전이 지정된 reference ABI, 파일별 세션, 지연 완료, 취소
- 동적 PDO 연결, 조회, 누락 처리, 재연결, 꺼내기
- 상위 필터의 요청 전달과 형식 안전 완료 처리
- MOF 기반 WMI 쿼리, 설정, 메서드, 이벤트 전달
- x64 드라이버에 대한 선택적 x86 애플리케이션

각 애플리케이션은 반환된 상태를 검증합니다. 디버그 출력은 통과 조건이 아닙니다.

## 아티팩트 구성

하나의 디렉터리 아래에 x64 패키지를 준비합니다.

```text
packages/
  basic/
  pnp/
  echo/
  reference/
  bus/
  filter-stack/
  wmi/
```

각 디렉터리에는 서명 또는 테스트 서명된 드라이버 패키지(`.sys`, INF, 카탈로그),
`.exe`, 그리고 샘플별 MOF 아티팩트가 들어 있습니다. `bus` 디렉터리에는 버스와
자식 기능 INF 패키지가 모두 있어야 합니다. x86 앱과 x64 드라이버의 호환성을
검증하려면 두 번째 루트 아래에 같은 디렉터리 이름으로 x86 애플리케이션을
준비하세요.

게스트는 이 패키지에 서명한 인증서를 이미 신뢰해야 합니다(또는 동등한 프로덕션
서명 체인으로 구성되어야 합니다). 이 모음은 인증서 저장소를 변경하거나 테스트
서명 모드를 켜지 않습니다.

## 일회용 VM 실행

관리자 권한 PowerShell 세션에서 실행합니다.

```powershell
.\Run-KmdfRuntimeSuite.ps1 `
  -PackageRoot C:\crtsys-kmdf-runtime\x64 `
  -X86AppRoot C:\crtsys-kmdf-runtime\x86
```

WMI 서비스를 의도적으로 사용할 수 없게 만든 게스트 이미지에서만 `-SkipWmi`를
사용하세요. 스크립트는 비승격 세션을 거부하고, 사용 전에 필요한 아티팩트를 모두
확인하며, `finally` 블록에서 루트 열거 방식으로 설치한 각 샘플을 제거합니다.

Driver Verifier 동시성 및 반복 로드 스트레스는 별도
[`verifier-stress`](../verifier-stress/README.ko-KR.md) 픽스처에서 다룹니다.
DMA, USB, PCI 및 장치 클래스 런타임 검증은 일치하는 하드웨어가 필요하므로 이
소프트웨어 전용 게이트에 포함하지 않습니다.

## 호스트 측 VM 인수 테스트

`Run-KmdfVmAcceptance.ps1`는 전체 호스트 워크플로입니다. 이 스크립트는 다음을
수행합니다.

1. x64 WDK 패키지와 x86 애플리케이션을 빌드하고 준비합니다.
2. 모든 소프트웨어 전용 샘플과 스트레스 픽스처를 `Oneboot` 표준 Driver Verifier
   구성으로 선택합니다.
3. 재부팅한 뒤 선택한 바이너리가 활성 상태임을 확인합니다.
4. 준비된 테스트 인증서만 일회용 게스트에 가져옵니다.
5. x64 및 WOW64 클라이언트로 런타임 모음을 실행합니다.
6. 반복 로드 주기에서 동시 verifier-stress 픽스처를 실행합니다.
7. 활성 Verifier 활동, 버그 체크 이벤트, 덤프, 장치 정리를 확인합니다.
8. 명시적으로 전달한 이전 Verifier 대상 및 부팅 모드를 복원하고 다시 재부팅합니다.

암호는 `SecureString` 매개 변수입니다. 스크립트는 `vmrun`을 호출할 때만 이를
평문으로 변환하고, 오류에서 자격 증명 인수를 숨기며, 생성 스크립트나 로그에
기록하지 않습니다.

```powershell
$vmxPath = Read-Host 'Path to the disposable test VMX'
$vmPassword = Read-Host 'VM encryption password' -AsSecureString
$guestUser = Read-Host 'Guest user'
$guestPassword = Read-Host 'Guest password' -AsSecureString
$existingVerifierTargets = @(
  # Copy exact .sys names from verifier /querysettings, if any.
)

.\Run-KmdfVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestUser $guestUser `
  -GuestPassword $guestPassword `
  -RestoreDriverFileName $existingVerifierTargets `
  -RestoreBootMode Persistent
```

기존 로컬 출력 집합을 검증할 때는 `Prepare-KmdfRuntimeArtifacts.ps1 -SkipBuild`를
사용하세요. 인수 테스트 래퍼도 같은 목적으로 `-SkipBuild`를 받습니다. 빈
`RestoreDriverFileName`은 이전 구성을 추측하는 대신 Verifier를 재설정합니다.

## 인수 기준

전체 호스트 인수 테스트는 다음 조건을 모두 충족해야 합니다.

- 모든 소프트웨어 전용 샘플이 x64 애플리케이션에서 통과하고, 준비되어 있다면
  WOW64 애플리케이션에서도 통과한다.
- 해당하는 모든 PnP 샘플이 장치 재시작 및 두 번째 애플리케이션 실행을 통과한다.
- 모든 임시 Verifier 대상이 활성화되어 드라이버 로드/언로드를 기록한다.
- 스트레스 픽스처가 구성한 취소 경쟁, 작업자, 로드 주기를 완료한다.
- 정리 후 테스트 장치, 서비스, 가져온 테스트 인증서, 크래시 이벤트, 새 덤프가
  남지 않는다.
- 호출자가 제공한 정확한 Verifier 대상 목록과 부팅 모드가 복원된다.

호스트 로그는 구성 가능한 `LogRoot`에 기록됩니다. 해당 디렉터리는 CI 또는 릴리스
아티팩트로 보관하세요.
