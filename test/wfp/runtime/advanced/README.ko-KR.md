# 고급 WFP VM 수용

이 게이트는 선택한 고급 WFP 샘플만 빌드, 패키징 및 테스트 서명하여 이미 실행
중인 일회용 Windows 게스트에 배치합니다. 그런 다음 선택한 모든 컨트롤러를
반복 실행하고 Driver Verifier의 로드/언로드 횟수, 충돌 이벤트와 덤프, 그리고
Verifier 구성이 변경되지 않았는지 검사합니다.

스크립트는 특정 환경에 종속되지 않습니다. VM 경로, 계정, 자격 증명, 게스트
배치 경로, 아티팩트 경로, SDK/도구 집합 버전 및 반복 횟수는 모두
매개변수입니다. 픽스처에는 체크아웃 경로나 VM ID가 내장되어 있지 않습니다.

VM 실행기에는 의도적으로 재부팅, 스냅샷, Driver Verifier 재설정 또는 구성
변경 기능이 없습니다. 실행 전에 운영자는 다음을 수행해야 합니다.

1. 선택한 모든 드라이버를 Verifier 대상으로 구성합니다.
2. 테스트 서명된 패키지에 필요하다면 **Disable driver signature enforcement**를
   선택하는 것을 포함해 게스트를 수동으로 부팅합니다.
3. 내용이 정확히 `CRTSYS_DISPOSABLE_TEST_GUEST`인
   `C:\crtsys-disposable-test-guest.sentinel` 파일을 만들어 이 시스템이
   일회용 테스트 게스트임을 표시합니다.

실행기는 제품군 실행 전후에 `verifier /query`와 `verifier /querysettings`를
읽습니다. 선택한 드라이버가 이미 Verifier 대상이 아니면 실행을 거부하고, 이번
실행에서 해당 드라이버의 로드 및 언로드 횟수가 증가할 것을 요구하며,
`verifier-load-unload-evidence.json`을 기록합니다. 또한 설정 텍스트가 완전히
동일해야 합니다. 모든 부팅과 Verifier 변경은 사용자가 직접 관리합니다.

`-RuntimeOnly`는 선택한 드라이버가 현재 Verifier 대상이 아닌 상태에서 동일
부팅으로 기능만 검사할 때 사용하는 명시적인 예외입니다. 일회용 게스트 보호,
충돌 이벤트와 덤프 기준선, Verifier 설정의 바이트 단위 비교는 유지하지만,
선택한 드라이버가 Verifier 아래에서 로드되었다고 요구하거나 주장하지 않습니다.
기본값은 엄격한 Verifier 게이트입니다.

저자원 시뮬레이션은 명시적으로 선택해야 하는 별도 게이트입니다. 운영자가 부팅과
Verifier 대상을 준비하며, 실행기는 Verifier 설정을 변경하거나 게스트를
재부팅하지 않습니다. 별도의 `kernel-browser-https-inspection` Random Low
Resources 실행에서 의도적 할당 실패 1회를 관찰했고, 드라이버와 프로세스가
남지 않는 fail-closed 정리, 새 충돌 이벤트와 덤프 없음, Verifier 설정 불변을
확인했습니다. 이 결과는 일반 Verifier 게이트와 별개이며, 일반 게이트의 통과만으로
저자원 시험까지 통과했다고 간주하지 않습니다.

결정론적 런타임 증명은 다음과 같습니다.

- `datagram-proxy`: IPv4 및 IPv6 프록시 UDP 소켓은 독점적으로
  리디렉션된 데이터그램이 있는 경우 두 원래 소켓 모두 이후에 수신됩니다.
  세션 범위 정책 제거;
- `async-inspection`: IPv4 및 IPv6 허용 및 차단 결정 통과
  ALE 재승인이 지연되고 차단된 두 연결 모두 이후 복구됩니다.
  제거;
- `flow-monitor`: 관찰 전용 IPv4 및 IPv6 콜아웃은 선택된 TCP를 보고합니다.
  읽기 전용 IOCTL을 통한 흐름 및 스트림 바이트 원격 측정;
- `stream-edit`: `BLOCKME`는 전송 호출에 걸쳐 분할되어 동일한 길이로 도착합니다.
  `REDACT!`는 정책 제거 후 변경 없이 통과합니다. 그것의 수용 정착물
  또한 IOCP 코루틴 읽기/쓰기/취소/EOF 및 제한된 동적 메시지를 실행합니다.
  프레임 계약. 정책
  반복에서는 동일한 `co_await read_exactly_borrowed()` 및 `write_all()` 페이로드를 사용합니다.
  별도의 차단 소켓 구현이 아닌 경로입니다.
- `connect-redirect`: 선택한 TCP 연결이 로컬 프록시에 도달합니다.
  프록시는 원래 엔드포인트와 불투명 리디렉션 레코드를 얻습니다.
  리디렉션 루프 없이 아웃바운드 구간을 연결하고 요청을 전달하며
  IPv4와 IPv6 모두에 대해 2개의 IOCP 코루틴을 사용한 응답 및 정책
  제거하면 두 제품군 모두에 대한 직접 연결이 복원됩니다.
- `bind-redirect`: 형식화된 IPv4 및 IPv6 ALE 바인드 요청이 다음으로 다시 작성됩니다.
  루프백 포트를 선택한 후 동적 바인딩 후 임시 바인딩으로 돌아갑니다.
  정책 제거.
- `tls-inspection-proxy`: 동일한 redirect handoff가 사용자 모드 Schannel 서버
  leg와 별도로 검증되는 Schannel client leg로 연결됩니다. 조각난 ClientHello를
  바탕으로 SNI별 CA 서명 leaf를 선택해 cache하고, 크기가 제한된 HTTP/1.1 framing이
  평문 본문을 노출합니다. `ALLOW`는 origin에 도달하고 `BLOCKME`는 HTTP 요청이
  origin에 도달하기 전에 거부됩니다. trust store는 변경하지 않으며 정책을 제거하면
  직접 TLS 연결이 복원됩니다.
- `udp-content-filter`: WFP는 완전한 아웃바운드 IPv4 및 IPv6 UDP를 처리합니다.
  데이터그램. 하나의 사용자 모드 코루틴 정책은 이를 허용/재주입하거나 차단합니다.
  제거하면 두 가족이 모두 복원됩니다. 일회성 자체 테스트를 통해 다음이 입증됩니다.
  지연, 잘못된 형식, 시간 초과 및 할당량 초과 판정 경로는 페일클로즈됩니다.
- `tcp-content-filter`: WFP는 명시적으로 전체 메시지를 수집합니다.
  선택된 인바운드 IPv4 및 IPv6 TCP 샘플 프로토콜 흐름. 공유된
  사용자 모드 정책은 허용된 프레임을 재개하고, 차단된 흐름을 삭제하고, 제거합니다.
  두 가족을 모두 복원합니다. 일회성 자체 테스트를 통해 잘못된 판정이 입증되었습니다.
  거부, 시간 초과 흐름 삭제 및 지연 허용 거부.
- `kernel-connect-redirect`: 드라이버는 허용된 WSK 소켓을 캡처합니다.
  원본 튜플 및 불투명 WFP 리디렉션 레코드, 이전 레코드 적용
  아웃바운드 연결을 수행하고 IPv4/IPv6 양방향 릴레이를 수행합니다.
  사용자 모드 데이터 플레인.
- `kernel-tls-inspection-proxy`: 드라이버 소유 WSK는 원래 튜플을 보존합니다.
  레코드를 리디렉션하고 커널 Schannel은 시스템과 함께 TLS 레그를 모두 종료합니다.
  원본 구간에 대한 유효성 검사 및 공통 변환 파이프라인이 검사 및
  제한된 HTTP/1.1 및 HTTP/2 트래픽을 다시 작성합니다. 수락이 허용됨이 증명됨
  트래픽이 IPv4/IPv6 TLS 원본에 도달했지만 차단된 트래픽은 도달하지 못했고 형식이 잘못되었으며
  유휴 ClientHello 경로가 닫히지 않았으며 커서 증거가 유지되었습니다.
- `kernel-browser-https-inspection`: 드라이버가 WSK, Schannel 및
  별도의 컨트롤러가 정책 및 드라이버 명령을 소유하는 동안 MsQuic 경로입니다.
  승인 장치는 통제된 ID를 발급하고 HTTP/1.1을 증명합니다.
  HTTP/2 및 HTTP/3은 다음을 통해 캡처된 요청/HTML 콘텐츠를 허용/차단합니다.
  제한된 제어 프로토콜. 브라우저 설정을 변경하거나
  브라우저.
- `kernel-http3-inspection`: 드라이버가 기존 받은 편지함 MsQuic NMR에 바인딩됩니다.
  공급자, TLS 1.3/QUIC 종료, HTTP/3 설정 교환, 제한된 디코딩
  QPACK을 수행하고 IPv4 및 IPv6를 통해 콘텐츠가 선택된 200/403 응답을 반환합니다.
- `kernel-udp-content-filter`: 전체 IPv4/IPv6 데이터그램을 검사하고
  드라이버가 직접 허용하거나 차단합니다.
- `kernel-tcp-content-filter`: 분할 IPv4/IPv6 바이트 스트림이 프레임화되고
  제한된 버퍼링을 사용하여 드라이버가 직접 허용하거나 차단합니다.

`Run-WfpAdvancedSuite.ps1 -SelectedSample stream-edit`를 실행할 수도 있습니다.
이미 준비된 일회용 테스트 게스트에서 직접. 명시적인 스위치
이 명령은
테스트 드라이버 서비스:

```powershell
.\Run-WfpAdvancedSuite.ps1 `
  -PackageRoot C:\wfp-advanced `
  -SelectedSample stream-edit `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```

이 동일 부팅 검사는 `stream-edit`만 로드하고 언로드하며, Verifier 설정을
변경하거나 재시작을 요청하지 않습니다.

`specialized-observation`은 기본적으로 결정적인 엔드포인트 종료 신호를
사용합니다. 실제 이더넷 분류 트래픽을 요구하려면 ICMP echo에 응답하는 접근
가능한 IPv4 주소를 전달하고 `-SpecializedObservationRequireMac`을 추가하십시오.
Hyper-V 토폴로지에서는 `-SpecializedObservationRequireVSwitch`도 사용할 수
있습니다.

```powershell
.\Run-WfpAdvancedVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestPassword $guestPassword `
  -SelectedWfpSample specialized-observation `
  -SpecializedObservationTrafficTarget '<reachable-ipv4>' `
  -SpecializedObservationRequireMac `
  -SpecializedObservationTrafficDurationMs 5000
```

실행기는 vSwitch를 만들거나 네트워크 토폴로지를 변경하지 않습니다. 운영자가
조건에 맞는 트래픽을 제공하고 필요한 증거를 명시적으로 선택해야 합니다.

## Hyper-V vSwitch 및 IPsec 증적

2026-08-10에 일회용 중첩 Hyper-V 시험 환경에서 환경 의존적인
`specialized-observation` 경로를 실행했습니다. Windows L1 게스트는 관리 OS
어댑터에 `192.168.250.1/24`를 설정한 내부 스위치를 호스팅했고, Windows L2
트래픽 피어는 같은 스위치에서 `192.168.250.2/24`를 사용했습니다. 드라이버,
Driver Verifier 및 임시 정책은 L1에서만 실행했습니다. L2는 트래픽 피어일 뿐이며
테스트 서명 부팅이나 Driver Verifier가 필요하지 않습니다.

vSwitch 게이트에서는 선택한 스위치의 기본 제공 **Microsoft Windows 필터링
플랫폼** 스위치 확장이 활성화되어 실행 중이어야 합니다. 이 전제 조건에서 3회
모두 `registered-mask=63`, `exercised-mask=63`, `required-mask=51`로 통과하여
IPv4/IPv6 엔드포인트와 vSwitch 양방향을 증명했습니다. 확장을 끈 진단 실행에서는
`exercised-mask=15`까지만 관찰됐으므로, 등록 성공이나 일반 ping만으로는 vSwitch
증적이 되지 않습니다. 증적에는 제품군 로그, 스위치 확장 상태 및 Verifier 전후
상태를 보존해야 합니다. 기록된 실행에서 선택한 드라이버의 Verifier 로드/언로드
횟수는 `2/2`에서 `3/3`으로 증가했고 Verifier 설정은 바이트 단위로 같았습니다.

IPsec 게이트는 두 피어 주소로 범위를 제한하고 TCP와 UDP를 각각 선택한 임시
컴퓨터 사전 공유 키 전송 모드 규칙을 사용했습니다. 실제 TCP 및 UDP nonce
트래픽으로 각 피어에 Quick Mode SA 4개, 즉 프로토콜별 인바운드·아웃바운드
항목을 생성했습니다. 이후 3회 드라이버 실행은 `registered-mask=63`,
`exercised-mask=63`, `required-mask=3`으로 통과했고 Driver Verifier의 로드와
언로드 횟수도 각각 `+1` 증가했으며 Verifier 설정은 그대로 유지됐습니다. 낮은
필수 마스크는 의도한 것입니다. IPsec 정책 계층은 관리 전용이므로 IPsec 증명은
보호된 TCP/UDP 트래픽과 실제 Quick Mode SA를 바탕으로 하며, IPsec 정책 계층을
분류 콜아웃으로 등록했다는 주장이 아닙니다.

재사용 가능한 증적에는 정책 목록, 양쪽 피어의 Quick Mode SA 전후 JSON,
TCP/UDP 수신 결과, `specialized-observation-ipsec.log`,
`verifier-load-unload-evidence.json` 및 범위가 제한된 정리 결과가 포함됩니다.
정리 작업은 시험 규칙 그룹, 인증 집합, 리스너 및 임시 방화벽 규칙만 제거해야
합니다. 기본 제공 `IKEEXT` 또는 `PolicyAgent` 서비스를 중지하거나 제거해서는
안 됩니다. 기록된 정리 결과에서는 양쪽 피어에서 시험 범위 객체가 모두
제거됐고, 두 기본 제공 서비스는 계속 실행 중이었으며, 임시 드라이버 서비스도
남지 않았습니다.

이 환경별 게이트는 계속 옵트인 및 읽기 전용입니다.

- `-RequireActiveIpsecSecurityAssociation`에는 `specialized-observation`과 보호된
  피어를 가리키는 `-SpecializedObservationTrafficTarget`이 필요합니다. 실행기는
  드라이버를 로드하기 전에 일치하는 Quick Mode SA가 있는지 확인하고, 특수 정책이
  활성화된 동안 해당 피어로 제한된 ICMP 트래픽을 보낸 뒤, 실행 후에도 같은 SA가
  있는지 다시 확인합니다. 증거는
  `ipsec-quick-mode-sa-before.json` 및
  `ipsec-quick-mode-sa-after.json`에 기록합니다. 운영자가 두 피어를 구성하고 SA를
  수립해야 하며, 실행기는 연결 보안 정책을 만들거나 제거하지 않습니다.
- `-RequireLowResourcesSimulation`에는 현재 부팅이 Random 또는
  Systematic Low Resources Simulation을 활성화한 상태여야 하며, 선택한 드라이버를
  하나라도 로드하기 전에 설정되어 있어야 합니다. Driver Verifier의 의도적 할당
  실패 횟수가 증가해야 하고 `low-resources-evidence.json`도 기록합니다. 측정된
  오류 주입 후 제품군 오류는 배치한 드라이버 서비스나 프로세스가 남지 않고,
  Verifier 설정이 바이트 단위로 동일하며, 충돌/덤프 사후 검사가 깨끗할 때만
  정상적인 실패 시 차단 결과로 인정합니다. 실행기는 여전히 Verifier를 변경하거나
  게스트를 재부팅하지 않습니다.

이러한 스위치는 일반적인 등록 전용 실행이 보고되는 것을 방지합니다.
IPsec 또는 할당 실패 런타임 증거로 사용됩니다.

`datagram-proxy`를 시작하면 UDP 헤더가 두 MDL에 걸쳐 있는 실제 NBL도
실행합니다. 따라서 드라이버가 Verifier 대상인 동안 이 계약이 실패하면 서비스
시작도 실패합니다. `flow-monitor`는 실제 WFP 관찰과 텔레메트리 경로만
시작합니다. 제한된 코루틴 판독기의 조각화, 시간 초과, 취소, EOF, 경쟁 판독기,
용량 한도 및 언로드 종료 대기 계약은 별도의 `test/net/kernel-contracts`
드라이버가 담당합니다.

실행기는 다음 옵션도 받습니다.

- `-SelectedWfpSample`: 한 번의 실행에서 드라이버, 빌드, 패키지 아티팩트 및
  컨트롤러의 범위를 제한합니다.
- `-Configuration` 및 `-BuildRoot`: 암시적 아티팩트 위치로 대체하지 않고,
  `-SkipBuild`와 함께 명시적으로 선택한 Debug 또는 Release 빌드 트리를
  재사용합니다.
- `-RuntimeOnly`: 선택한 드라이버가 Verifier 대상이었다고 주장하지 않으면서
  실행하되, 충돌/덤프 검사와 읽기 전용 Verifier 설정 비교를 유지합니다.
- `-MsQuicDllPath`: 커널 HTTP/3 컨트롤러에 호환되는 공식 `msquic.dll`을
  제공합니다. 생략하면 아티팩트 준비 단계가 NuGet에서 고정된
  `Microsoft.Native.Quic.MsQuic.Schannel` 패키지 버전을 가져옵니다.
- `-ManagedHttp3Url`: 브라우저 HTTP/3 WFP 드라이버와 관리형 클라이언트
  리디렉션 테스트를 추가합니다.

`Run-WfpAcceptanceMatrix.ps1`는 동일한 읽기 전용 부팅/검증 프로그램을 적용합니다.
모든 JSON 행을 계약합니다. 참조된 각 VM은 이미 다음을 사용하여 실행되고 있어야 합니다.
Verifier 대상으로 구성된 행의 선택된 드라이버. 매트릭스 스키마
따라서 다시 시작, 복원 또는 리소스 부족 옵션이 없습니다. 그것은 단지 운반
환경 제약, 샘플 선택 및 일회용 게스트 센티넬
경로.

커널 HTTP/3 게이트는 해당 컨트롤러에서 사용하는 사용자 모드 DLL만 준비합니다.
`msquic.sys`를 설치하거나 교체하지 않습니다. 게스트는 이미 다음 사항을 제공해야 합니다.
받은 편지함 커널 MsQuic 공급자이며 프리플라이트가 없으면 명시적으로 실패합니다.

관리되는 HTTP/3 튜플 변환 경로는 의도적으로
원래 대상 컨텍스트: 관리되는 클라이언트가 이미 SNI를 소유하고 있으며
`:authority`, 프록시는 절대 호출하지 않습니다.
`SIO_QUERY_WFP_CONNECTION_REDIRECT_CONTEXT`. 일반 TCP 브라우저와
연결-리디렉션 경로는 프록시에 다음이 필요하기 때문에 해당 컨텍스트를 유지합니다.
원래 끝점.

## 동일 부팅 흡수 및 타사 공존

`Run-WfpAdvancedSoak.ps1`는 이미 검증된 로드를 반복적으로 호출합니다.
언로드 레이스, 페일클로즈, 듀얼 스택, 리디렉션, TLS 및 20,000 흐름 계약
요청한 벽시계 기간 동안. 검증자 상태는 변경되지 않으며 변경되지 않습니다.
기계를 다시 시작하지 마십시오. 각 사이클은 경과 시간을 기록하며, 흐름 모니터의
보고된 IPv4/IPv6 p95 대기 시간, 사용 가능한/커밋된 메모리, 페이징 및 비페이징
풀, 프로세스/스레드 수, WFP 이전 및 이후 상태, 충돌 이벤트 및 신규
머신-로컬 증거 디렉터리에 파일을 덤프합니다.

VPN, 방화벽 또는 WebFilter 제품이 설치된 테스트 시스템에서는
`-RequiredProviderPattern`을 사용하십시오. 제공한 모든 정규식 패턴이 두 WFP
스냅샷에 모두 있어야 하므로, 이 제품군은 지정한 공급자가 등록된 상태에서
NTL이 실행되었음을 입증합니다. 다만 실제 공존을 입증하려면 해당 제품과
대표 트래픽이 준비된 시스템에서 시험해야 합니다. 스크립트는 이러한 증거를
인위적으로 만들지 않습니다.

```powershell
.\Run-WfpAdvancedSoak.ps1 `
  -PackageRoot C:\wfp-advanced `
  -DurationMinutes 480 `
  -IterationsPerCycle 3 `
  -RequiredProviderPattern 'Contoso VPN','Enterprise Web Filter' `
  -EvidenceDirectory C:\wfp-evidence\soak-8h `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```
