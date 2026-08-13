# 라이브 HTTPS 및 브라우저 검사 테스트

이 인터넷 의존 테스트는 결정적인 WFP 단위·컴파일 테스트와 분리되어 있습니다.
DNS, 공개 사이트의 동작, 승인된 기업 HTTPS 필터에 의존합니다. 재부팅을 요청하거나
Driver Verifier 설정을 변경하지 않습니다.

이 디렉터리에는 별도의 결정적이며 드라이버가 필요 없는 HTTP/3 허용성 검사도
있습니다. 두 루프백 구간에서 실제 msh3/MsQuic H3을 사용하며 인터넷, 브라우저,
WFP, 신뢰 저장소 쓰기에 의존하지 않습니다.

```powershell
.\Prepare-ControlledHttp3Artifacts.ps1
.\Start-ControlledHttp3EndToEnd.ps1 `
    -PackageRoot ..\..\..\..\artifacts\controlled-http3-staging `
    -Concurrency 8
```

`Run-ControlledHttp3VmAcceptance.ps1`는 그 최소 패키지만 VM에 복사합니다.
추가로 Driver Verifier, 부팅 시각, 루트 저장소, 임시 키, WFP 서비스, 충돌 이벤트,
덤프, 남은 프로세스를 전후 비교합니다. [한국어 제어 H3 가이드](./CONTROLLED-HTTP3-README.ko-KR.md)를
참조하세요.

`Run-WfpHttpsVmAcceptance.ps1`는 하나의 휴대용 패키지를 VMware Workstation에
복사하고 한 번의 게스트 작업 세션에서 네 가지 검사를 실행할 수 있습니다. 브라우저
검사는 VM 운영자가 이미 열어 둔 브라우저를 관찰하며, 브라우저를 시작·종료·구성하지
않습니다.

1. 제어 호스트 TCP 리디렉션과 평문 일치
2. 캡처한 HTML을 사용한 일반 브라우저 HTTPS 검사
3. 일반 브라우저 TCP 대체 경로를 사용하는 실패 시 차단 WFP UDP/443 정책
4. 애플리케이션 소유 신뢰를 사용하는 선택적 관리형 클라이언트 HTTP/3 검사

스크립트는 임시 CA 신뢰와 드라이버 서비스를 제거하고, 실행 전후 Driver Verifier
설정을 비교하며, 새 충돌 이벤트와 덤프를 검사한 뒤 `artifacts` 아래에 증거
아카이브를 복사합니다. 별도의 게스트 사후 검사는 남아 있는 샘플 서비스, 프로세스,
검사용 CA가 있으면 실패합니다.

## 휴대용 패키지 준비

개발 머신에서 다음을 실행합니다.

```powershell
.\Prepare-WfpHttpsLiveArtifacts.ps1
```

이 명령은 `tls-inspection-proxy`와 `browser-https-inspection` x64 드라이버를
빌드·테스트 서명하고, 드라이버, 애플리케이션, INF, 서명 인증서, 고정된 msh3/MsQuic
런타임 DLL, 고지, 스크립트를 `artifacts\wfp-https-live-staging` 아래에 준비합니다.

게스트는 환경에서 사용하는 테스트 서명 정책을 이미 허용해야 합니다. 드라이버 서명
인증서와 임시 HTTPS 검사 CA는 서로 다른 인증서입니다. 허용성 검사 실행기는 필요한
공개 인증서만 설치하고 임시 검사 CA는 제거합니다.

드라이버를 설치하거나 컴퓨터 인증서 저장소에 쓰는 모든 스크립트에는 의도적인 게스트
확인 두 가지가 필요합니다. 일회용 VM 안에 센티널을 한 번 만들고, 제품군 명령을 직접
실행할 때 승인 스위치를 전달합니다.

```powershell
Set-Content C:\crtsys-disposable-test-guest.sentinel `
    'CRTSYS_DISPOSABLE_TEST_GUEST' -NoNewline
```

스크립트는 이 센티널을 만들지 않습니다. `Run-WfpHttpsVmAcceptance.ps1`는 게스트를
변경하기 전에 이를 확인하고 호출자가 브라우저 URL을 제공하도록 요구합니다. 기본으로
선택되는 공개 호스트는 없습니다.

## 제어 호스트 검사

```powershell
.\Run-WfpHttpsLiveTest.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -HostName $env:NTL_WFP_TEST_HOST `
    -AllowDisposableGuestMutation `
    -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

컨트롤러는 서로 다른 IPv4 DNS 결과를 시도하고 Schannel로 원본 서버를 검증하며,
프록시가 관찰한 평문과 제어된 클라이언트 응답을 비교합니다.
`-AllowUnavailableRevocation`은 사용할 수 없거나 오프라인인 해지 정보만 허용하며,
신뢰되지 않은 인증서, 만료, 이름 불일치, 실제 해지된 인증서는 허용하지 않습니다.

## 브라우저 검사

복사한 패키지의 관리자 권한 PowerShell에서 실행합니다.

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

래퍼를 시작하기 전에 브라우저를 일반 방식으로 열고 프로세스를 계속 실행해 두세요.
임시 프로필이나 테스트 플래그를 제공하지 마세요. 래퍼는 실행 파일 경로(기본값은 Edge)를
찾고 샘플 드라이버와 앱만 시작하며, 준비를 기다린 뒤 생성한 검사 CA를 일시적으로
신뢰합니다. 브라우저를 시작·종료하지 않고 프로필, 기능, 인증서, QUIC, ECH, 로깅
인수를 제공하지도 않습니다. 관찰 간격이 활성화된 동안 이미 열린 창에서 탐색하세요.

`-Urls`는 기대하는 캡처를 나열할 뿐 브라우저를 탐색하지 않습니다. 생략해도 새로
검사한 HTML 응답이 하나 이상 필요합니다. `-RequireQuicBlockedFallback`에는 다음이
모두 필요합니다.

- 애플리케이션 범위 IPv4 및 IPv6 UDP/443 네이티브 차단 필터가 제한된 WFP 인벤토리
  검사를 통과한다.
- 런타임이 출력한 각 필터 ID가 같은 실행의 인벤토리에 존재한다.
- 관찰한 브라우저, UDP/443, 해당 네이티브 필터 ID 중 하나에 대한 WFP
  `classify_drop` 네트 이벤트가 관찰된다.
- `-Urls`로 제공한 모든 호스트를 포함해 새로 검사한 HTML이 TCP를 통해 캡처된다.

증거 디렉터리에는 `wfp-policy-diagnostics.log`,
`browser-transport-evidence.json`, 프록시 로그, 캡처한 HTML이 포함됩니다.
브라우저 NetLog와 콜아웃 `action_write` 카운터는 의도적으로 허용성 검사 증거로
사용하지 않습니다. 일치하는 UDP/443 drop 이벤트가 없는 실행은 결론을 낼 수 없으며,
QUIC 대체가 발생했다고 주장하지 않고 판정을 실패시킵니다.

`-DurationSeconds`가 없으면 수동으로 탐색한 뒤 Enter를 눌러 중지합니다. 시간을
지정한 실행은 요청한 전체 간격을 관찰하며 첫 번째 캡처 후 중지하지 않습니다. 판정
스위치를 생략해도 런타임 정책은 바뀌지 않지만, 관찰한 UDP 시도가 없는 실행은 실패
대신 `NOT_OBSERVED`를 보고할 수 있습니다.

일반 드라이버는 애플리케이션 범위 IPv4/IPv6 TCP 443을 Schannel 프록시로
리디렉션하고 UDP 443을 차단합니다. Chromium은 QUIC에 사용자 지정 검사 CA를
허용하지 않으므로, 변경하지 않은 Edge를 사설 CA QUIC 서버로 리디렉션하면 TLS
`certificate_unknown`로 끝납니다. UDP를 차단하면 브라우저 설정을 바꾸지 않고도
Edge가 검사되는 TCP 대체 경로를 사용합니다.

일반 브라우저 경로는 관리형 브라우저 신뢰 정책이나 사이트별 인증서 예외에 의존하지
않습니다. 이는 자체 관리형 클라이언트를 설치하거나 브라우저와 통합하는 제품에는
일반화되지 않습니다.

## 관리형 클라이언트 HTTP/3 검사

이 경로는 WFP 브라우저 정책과 별개입니다. 드라이버를 로드하지 않고, 브라우저를
시작·구성하지 않으며, 검사 CA를 Windows 신뢰 저장소에 쓰지 않습니다.

```powershell
$inspectionUrl = [uri](Read-Host 'HTTPS URL to inspect')
.\Start-ManagedHttp3Inspection.ps1 `
    -PackageRoot C:\crtsys-wfp-https `
    -Url $inspectionUrl `
    -LogDirectory C:\crtsys-wfp-https\managed-http3-log
```

NTL 클라이언트는 명시적인 루프백 검사 엔드포인트에 연결하는 동안 요청한 SNI와
`:authority`를 유지합니다. 래퍼가 메모리로 제공한 CA로 엔드포인트를 검증합니다.
래퍼는 실제 다운스트림 HTTP/3 요청과 캡처한 HTML을 판정합니다.

원본 서버 구간은 HTTP/3을 우선하고 검증합니다. 외부 QUIC 전송·연결·시간 초과
실패는 정상 검증한 TLS/TCP로 다시 시도할 수 있으며, 요청별 이벤트는 실제 업스트림이
`h3`, `h2`, `http/1.1` 중 무엇이었는지 기록합니다. 인증서, mTLS, 요청 검증 실패는
대체하지 않습니다.

같은 VM 세션에서 일반 브라우저 대체 경로 검사 뒤 이 검사를 실행하려면
`Run-WfpHttpsVmAcceptance.ps1`에 `-IncludeManagedHttp3`를 추가하세요.

`Run-WfpHttpsVmAcceptance.ps1`에서는 운영자가 실행기를 시작하기 전에 일반
브라우저를 열고, 게스트 브라우저 관찰 간격이 활성화된 동안 `-BrowserUrl`로
탐색해야 합니다. `-BrowserUrl`은 브라우저 시작 명령이 아니라 기대하는 캡처입니다.

TCP 경로는 제한된 HTTP/1.1, 다중화 HTTP/2, WebSocket `permessage-deflate`를
지원합니다. HTTP/1.1, HTTP/2, 일반 HTTP/3 응답은 해당하는 경우 공통의 제한된 gzip,
zlib `deflate`, Brotli 디코더를 사용합니다. 캡처한 `.html` 파일은 렌더링한 DOM
스냅샷이 아니라 서버 응답 본문입니다.

증거 파서와 브라우저 변경 금지 규칙에는 오프라인 계약이 있습니다.

```powershell
.\Test-WfpBrowserTransportEvidenceContract.ps1
.\Test-WfpBrowserWrapperContract.ps1
```

후자는 일반 브라우저 래퍼나 VM 실행기에 브라우저 시작/종료, 일회용 프로필, NetLog,
인증서 우회, QUIC/ECH 기능, 호스트 매핑 인수가 다시 들어오면 거부합니다.

## 보안 및 지원하지 않는 경계

WFP는 프로필이 아니라 실행 파일 경로로 범위를 지정합니다. 필터가 활성화된 동안
선택한 브라우저 실행 파일을 사용하는 모든 프로세스가 범위에 들어가므로 전용 VM을
사용하세요. 캡처한 콘텐츠에는 기밀 정보가 포함될 수 있습니다.

기존의 승인된 기업 HTTPS 필터는 프록시의 원본 서버 검증을 위해 CA를 Windows에서
신뢰해야 합니다. 이 신뢰가 NTL에 임의 ECH 키를 부여하거나, 인증서 고정을 우회하거나,
원본 서버 mTLS ID를 선택할 권한을 주지는 않습니다.

런타임에는 ECH 프런트엔드 결과, 다운스트림 고정 지식, 정확한 SNI 원본 서버
클라이언트 인증서용 명시적 공급자가 있습니다. 임의 ECH에는 여전히 일치하는 ECH
비공개 구성과 복호화·종료를 소유하는 TLS 프런트엔드가 필요합니다. 인증서 고정은
우회할 수 없습니다. 고정된 msh3 백엔드는 제한된 NTL 프로토콜 계약을 별도로
테스트하더라도 실시간 확장 CONNECT/WebTransport에 필요한 원시 QUIC 스트림과
Datagram 콜백을 노출하지 않습니다.
