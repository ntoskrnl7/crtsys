# 라이브 HTTPS 및 브라우저 검사 테스트

이 인터넷 의존 테스트는 결정론적인 WFP 단위/컴파일 테스트와 분리되어 있습니다.
DNS, 공개 사이트의 동작, 승인된 기업 HTTPS 필터에 의존합니다. 재부팅을 요청하거나
Driver Verifier 설정을 바꾸지 않습니다.

또한 디렉터리에는 별도의 결정적이며 드라이버가 없는 HTTP/3이 포함되어 있습니다.
수락. 두 루프백 다리 모두에서 실제 msh3/MsQuic H3을 사용하지만
인터넷, 브라우저, WFP 또는 신뢰 저장소 쓰기에 따라 다릅니다.

```powershell
.\Prepare-ControlledHttp3Artifacts.ps1
.\Start-ControlledHttp3EndToEnd.ps1 `
    -PackageRoot ..\..\..\..\artifacts\controlled-http3-staging `
    -Concurrency 8
```

`Run-ControlledHttp3VmAcceptance.ps1`는 해당 최소 패키지만
VM. 또한 드라이버 확인 프로그램, 부팅 시간, 루트 저장소를 비교합니다.
임시 키, WFP 서비스, 충돌 이벤트, 덤프 및 나머지 프로세스.
[한국어 제어 H3 가이드](./CONTROLLED-HTTP3-README.ko-KR.md)를 참조하세요.

`Run-WfpHttpsVmAcceptance.ps1`는 하나의 휴대용 패키지를 VMware에 복사합니다.
Workstation을 사용하며 하나의 게스트 작업 세션에서 4개의 검사를 실행할 수 있습니다. 는
브라우저 검사는 VM 운영자가 이미 연 브라우저를 관찰합니다. 그렇죠
브라우저를 시작, 중지 또는 구성하지 마십시오.

1. 제어된 호스트 TCP 리디렉션 및 일반 텍스트 동등성;
2. 캡처된 HTML을 사용한 일반 브라우저 HTTPS 검사;
3. 일반 브라우저 TCP 폴백을 사용하는 장애 차단 WFP UDP/443 정책 그리고
4. 애플리케이션 소유 신뢰를 통한 선택적 관리형 클라이언트 HTTP/3 검사.

스크립트는 임시 CA 신뢰와 드라이버 서비스를 제거하고, 실행 전후의 Driver
Verifier 설정을 비교하며, 새 충돌 이벤트와 덤프를 검사한 뒤 증거 아카이브를
`artifacts` 아래에 복사합니다. 별도의 게스트 사후 검사는 남아 있는 샘플
서비스, 프로세스 또는 검사 CA가 있으면 실패합니다.

## 휴대용 패키지를 준비하세요

개발 머신에서:

```powershell
.\Prepare-WfpHttpsLiveArtifacts.ps1
```

이는 `tls-inspection-proxy`를 빌드하고 테스트 서명하며
`browser-https-inspection` x64 드라이버 및 드라이버, 애플리케이션,
INF, 서명 인증서, 고정된 msh3/MsQuic 런타임 DLL, 알림 및
`artifacts\wfp-https-live-staging` 아래의 스크립트.

게스트는 환경에서 사용되는 테스트 서명 정책을 이미 허용해야 합니다.
드라이버 서명 인증서와 임시 HTTPS 검사 CA는
다른 인증서. Acceptance Runner는 필요한 것만 설치합니다.
공용 인증서를 제거하고 임시 검사 CA를 제거합니다.

드라이버를 설치하거나 시스템 인증서 저장소에 쓰는 모든 스크립트에는 의도적인
게스트 확인 절차 두 가지가 필요합니다. 일회용 VM 안에 센티널을 한 번 만들고,
제품군 명령을 직접 실행할 때 승인 스위치를 전달하십시오.

```powershell
Set-Content C:\crtsys-disposable-test-guest.sentinel `
    'CRTSYS_DISPOSABLE_TEST_GUEST' -NoNewline
```

스크립트는 이 센티널을 생성하지 않습니다. `Run-WfpHttpsVmAcceptance.ps1`은
게스트를 변경하기 전에 센티널을 확인하고 호출자가 브라우저 URL을 지정하도록
요구합니다. 기본 공개 호스트는 선택되어 있지 않습니다.

## 제어 호스트 확인

```powershell
.\Run-WfpHttpsLiveTest.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -HostName $env:NTL_WFP_TEST_HOST `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

컨트롤러는 고유한 IPv4 DNS 결과를 시도하고 다음을 사용하여 원본을 검증합니다.
Schannel을 사용하고 프록시 관찰 일반 텍스트를 제어된 클라이언트와 비교합니다.
응답. `-AllowUnavailableRevocation`는 사용할 수 없는/오프라인만 허용합니다.
철회 데이터; 신뢰할 수 없거나 만료되었거나 이름이 일치하지 않거나
적극적으로 인증서가 취소되었습니다.

## 브라우저 검사

복사된 패키지의 관리자 권한 PowerShell에서 실행합니다.

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-WfpBrowserHttpsInspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Urls @($inspectionUrl) `
    -RequireQuicBlockedFallback `
    -LogDirectory C:\crtsys-wfp-https\browser-log `
    -DurationSeconds 90 `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

래퍼를 시작하기 전에 브라우저를 정상적으로 열고 그 프로세스를 실행 상태로 두세요.
임시 프로필이나 테스트 플래그를 제공하지 마세요. 래퍼가 찾는 것은
실행 경로(기본적으로 Edge), 샘플 드라이버와 앱만 시작
준비가 되기를 기다리고 생성된 검사 CA를 일시적으로 신뢰합니다. 그것
브라우저를 시작하거나 종료하지 않으며 프로필, 기능, 인증서, QUIC, ECH, 로깅 인수를
전달하지 않습니다. 관찰 간격이 활성화된 동안 이미 열린 창에서 탐색하세요.

`-Urls`는 예상되는 캡처를 나열합니다. 브라우저를 탐색하지 않습니다. 그렇다면
생략해도 새로 검사한 HTML 응답이 하나 이상 필요합니다.
`-RequireQuicBlockedFallback`에는 다음이 모두 필요합니다.

- 애플리케이션 범위 IPv4 및 IPv6 UDP/443 기본 블록 필터는
  제한된 WFP 재고 확인;
- 런타임에 의해 인쇄된 각 필터 ID는 해당 항목의 인벤토리에 존재합니다.
  같은 실행;
- 관찰된 브라우저, UDP/443에 대한 WFP `classify_drop` 네트 이벤트 및 하나
  정확한 기본 필터 ID 중 하나가 관찰됩니다. 그리고
- 새로 검사된 HTML은 제공된 모든 호스트를 포함하여 TCP를 통해 캡처됩니다.
  `-Urls`를 통해.

증거 디렉토리에는 `wfp-policy-diagnostics.log`,
`browser-transport-evidence.json`, 프록시 로그 및 캡처된 HTML입니다. 브라우저
NetLog 및 콜아웃 `action_write` 카운터는 의도적으로 허용되지 않습니다.
증거. 일치하는 UDP/443 삭제 이벤트가 없는 실행은 결론이 나지 않으며 실패합니다.
QUIC 폴백이 발생했다고 주장하는 대신 어설션을 사용합니다.

`-DurationSeconds`가 없으면 수동으로 찾아보고 Enter를 눌러 중지합니다. 시간 초과
요청된 전체 간격 동안 관찰을 실행합니다. 첫 번째 이후에는 멈추지 않습니다
캡처. 어설션 스위치를 생략해도 런타임 정책은 변경되지 않지만
관찰된 UDP 챌린지 없이 실행하여 대신 `NOT_OBSERVED`를 보고하도록 허용합니다.
실패의.

일반 드라이버는 애플리케이션 범위 IPv4/IPv6 TCP 443을 다음으로 리디렉션합니다.
Schannel 프록시 및 UDP 443을 차단합니다. Chromium은 사용자 정의를 허용하지 않습니다.
QUIC에 대해 CA를 검사하므로 변경되지 않은 Edge를 개인 CA QUIC로 리디렉션합니다.
서버는 TLS `certificate_unknown`로 끝납니다. UDP를 차단하면 Edge에서
브라우저 설정을 변경하지 않고 TCP 대체를 검사했습니다.

스톡 브라우저 경로는 관리되는 브라우저 신뢰 정책이나
사이트별 인증서 예외. 이는 제품에 일반화되지 않습니다.
자체 관리 클라이언트를 설치하거나 브라우저와 통합합니다.

## 관리형 클라이언트 HTTP/3 검사

이 경로는 WFP 브라우저 정책과 별개입니다. 드라이버를 로드하지 않습니다.
브라우저를 시작 또는 구성하거나 Windows 트러스트에 검사 CA를 작성합니다.
매장:

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Url $inspectionUrl `
    -LogDirectory C:\crtsys-wfp-https\managed-http3-log
```

NTL 클라이언트는 연결하는 동안 요청된 SNI 및 `:authority`를 유지합니다.
명시적인 루프백 검사 엔드포인트. 엔드포인트의 유효성을 검사합니다.
래퍼가 제공하는 CA가 메모리에 있습니다. 래퍼는 실제 다운스트림을 주장합니다.
HTTP/3 요청 및 캡처된 HTML.

원본 레그는 HTTP/3을 선호하고 확인합니다. 외부 QUIC 전송,
연결 또는 시간 초과 실패는 일반적으로 검증된 TLS/TCP를 사용하여 재시도할 수 있습니다.
요청별 이벤트는 실제 업스트림이 `h3`, `h2`인지 여부를 기록합니다.
`http/1.1`. 인증서, mTLS 및 요청 유효성 검사 실패는 그렇지 않습니다.
대체.

이 검사를 실행하려면 `Run-WfpHttpsVmAcceptance.ps1`에 `-IncludeManagedHttp3`를 추가하세요.
동일한 VM 세션에서 재고 브라우저 대체 확인 후.

`Run-WfpHttpsVmAcceptance.ps1`의 경우 운영자는 일반
러너를 시작하기 전에 브라우저를 실행하고 `-BrowserUrl`로 이동하는 동안
게스트 브라우저 관찰 간격이 활성화되었습니다. `-BrowserUrl`가 예상됩니다.
브라우저 시작 명령이 아닌 캡처입니다.

TCP 경로는 제한된 HTTP/1.1, 다중화된 HTTP/2 및 WebSocket을 지원합니다.
`permessage-deflate`. HTTP/1.1, HTTP/2 및 일반 HTTP/3 응답은
해당되는 경우 공유 제한된 gzip, zlib `deflate` 및 Brotli 디코더.
캡처된 `.html` 파일은 렌더링된 DOM 스냅샷이 아닌 서버 응답 본문입니다.

증거 파서 및 브라우저 변경 없음 규칙에는 오프라인 계약이 있습니다.

```powershell
.\Test-WfpBrowserTransportEvidenceContract.ps1
.\Test-WfpBrowserWrapperContract.ps1
```
후자는 브라우저 시작/종료, 일회용 프로필, NetLog,
인증서 우회, QUIC/ECH 기능 및 호스트 매핑 인수(있는 경우)
일반 브라우저 래퍼 또는 VM 실행기에 다시 도입되었습니다.

## 보안 및 지원되지 않는 경계

프로필이 아닌 실행 경로로 WFP 범위를 지정합니다. 전용 VM을 사용하세요.
선택한 브라우저 실행 파일을 사용하는 프로세스는 범위 내에 있지만 필터는
활동적입니다. 캡처된 콘텐츠는 기밀일 수 있습니다.

기존의 승인된 기업 HTTPS 필터에는 CA가 신뢰해야 합니다.
프록시의 원본 유효성 검사를 위한 Windows입니다. 해당 신뢰는 NTL 키를 부여하지 않습니다.
임의 ECH의 경우 인증서 고정을 우회하거나 원본 mTLS를 선택하세요.
정체성.

런타임에는 다운스트림 ECH 프런트엔드 결과에 대한 명시적인 공급자가 있습니다.
고정 지식 및 정확한 SNI 원본 클라이언트 인증서. 임의 ECH
여전히 일치하는 ECH 비공개 구성과 TLS 프런트엔드가 필요합니다.
암호 해독 및 종료를 소유합니다. 고정을 우회할 수 없습니다. 고정된 msh3
백엔드는 필요한 원시 QUIC 스트림 및 데이터그램 콜백을 노출하지 않습니다.
제한된 NTL 프로토콜이더라도 라이브 확장 CONNECT/WebTransport의 경우
계약은 독립적으로 테스트됩니다.
