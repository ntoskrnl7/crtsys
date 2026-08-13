# NTL 미니필터 런타임 테스트

이 문서는 온보딩 예제가 아니라 VM 통합 테스트입니다. 작업별 타입이 지정된 콜백, 캐시된
파일 이름, 네 종류의 Filter Manager 컨텍스트 수명 단위, 타입이 지정된 RAII 완료 상태,
조건부 WhenSafe 처리 및 같은 볼륨에 연결한 두 개의 명명된 필터 인스턴스를
의도적으로 결합합니다.

자동 인스턴스는 개발 altitude `370030.227`을 사용합니다. 앱은 secondary definition을
`370030.228`에 연결하고 타입이 지정된 통신 포트와 shared ring을 검사합니다. 이어서
create, read, write, rename, cleanup, close 작업을 수행하고 인스턴스를 분리합니다.
통신 검증 범위에는 계약 검색과 자동 파생 schema 검증, 디코드 전 권한 검사, 동시
비동기 호출, 협력적 취소, 일시적·재생 가능 알림, 제한된 reliable queue 할당량,
타입이 지정된 stream 업로드/다운로드와 배치, ACK 재생을 포함한 명시적 세션
보존/재개, 채널 간 우선순위, 취소한 notification wait 정리, 연결/세션 거부
할당량, reliable byte 할당량, 잘못되거나 지나치게 큰 framing, 잘못된 공유 영역의
범위·접근·할당량 token 및 오래된 공유 영역 token이 포함됩니다. 임시 클라이언트가
반환한 stream 객체가 기반 연결을 계속 유지하는지도 검사합니다. C++20 빌드는
메서드, 알림, stream receive에 대해 타입이 지정된 `co_await` 호출도 실행합니다.
드라이버는 언로드 중 컨텍스트와 작업 카운터를 기록합니다.

런타임 앱은 명시적 altitude로 secondary instance를 연결하고 분리합니다. 별도의
보안 probe는 Administrators 보안 설명자가 관리자에게는 접근을 허용하고 임시 일반
사용자 계정은 거부하는지 확인합니다. 일반 앱은 schema hash 불일치를 정상 트래픽
처리 전에 거부하는지도 검증합니다.

소유권 검증은 실제로 로드한 드라이버에서 Microsoft 예제의 네 가지 메커니즘도
실행합니다.

- transaction context를 만들고 transacted create에 enlist하며 commit-finalize,
  rollback 및 소멸자 카운터를 검사합니다.
- NTFS/ReFS 인스턴스가 data scanning을 등록하고 읽기 전용 section을 생성·매핑한
  뒤 overwrite conflict를 받습니다. notification 콜백에서 section을 닫고 section
  context를 소멸시킵니다.
- 생성한 callback data로 동기 및 비동기 형식화 파일 조회를 수행하고 directory
  notification 요청을 보류합니다. 이를 `async_callback_data_operation`으로
  취소하여 `STATUS_CANCELLED`를 확인한 뒤 핸들을 해제하고 정상 언로드합니다.
- 앱이 시작한 overlapped directory notification은 타입이 지정된 하위 스택 operation
  status 콜백을 요청하고 `STATUS_PENDING`을 확인합니다. 읽기 전용 directory-control
  snapshot을 검증하고 비페이지 요청 상태를 정확히 한 번 소멸시킨 뒤 알림을
  취소하고 정상 언로드합니다.

생성 I/O 테스트는 의도적으로 `FltSetCancelCompletion`을 사용하지 않습니다. 이
루틴은 미니필터가 post 처리하는 기존 수신 IRP를 전제로 합니다. 생성 callback
data에는 `FltPerformAsynchronousIo`로 제출하기 전까지 기반 IRP가 없습니다.

create 경로는 플래그 없는 일반 post와 함께 `on_with_completion<T>()`를 사용합니다.
read 경로는 플래그 없는 즉시 post가 `post_continuation::when_safe`를 반환하면서 같은
형식화 상태를 사용합니다. safe 콜백은 같은 상태를 `IRQL <= APC_LEVEL`에서 받습니다.
NTL은 직접 또는 safe 처리 뒤, 스케줄링 실패 시, 또는 drain 중에 각 객체를 직접
소멸시킵니다. 언로드 로그는 생성·관찰·소멸 카운터를 보고하므로 VM 실행에서
온보딩 예제를 스트레스 테스트로 바꾸지 않고도 수명 회귀를 찾을 수 있습니다.

같은 CMake 프로젝트는 서로 격리된 기능 테스트 일곱 개도 빌드합니다.

- I/O buffer 쌍: [`IO-BUFFER-README.md`](IO-BUFFER-README.ko-KR.md)
- NameChanger 쌍: 타입이 지정된 name-provider 등록, create redirect, 이름 생성/정규화,
  directory graft 표시, hard-link 정보, 이름이 포함된 USN/Find-by-SID/cluster FSCTL
  결과: [`NAME-CHANGER-README.md`](NAME-CHANGER-README.ko-KR.md)
- SimRep 쌍: 타입이 지정된 simulated reparse, network-query-open 대체 경로,
  rename/hard-link 목적지 복구 및 tunneled-name 소유권:
  [`SIMREP-README.md`](SIMREP-README.ko-KR.md)
- delete 쌍: 타입이 지정된 legacy/extended disposition, create 시 delete-on-close, cleanup
  확인, 강제 disposition 경합 및 alternate-stream 분류:
  [`DELETE-README.md`](DELETE-README.ko-KR.md)
- Scanner/AvScan 쌍: post-create 취소, 보류한 형식화 write 정책, cleanup 재검사,
  data-scan section 및 TxF 알림:
  [`SCANNER-README.md`](SCANNER-README.ko-KR.md)
- MetadataManager 쌍: 암시적/명시적 락, snapshot, dismount, detach, remount 전반의
  볼륨별 메타데이터 소유권:
  [`METADATA-README.md`](METADATA-README.ko-KR.md)
- CDO 쌍: 미니필터 시작/언로드 수명이 소유하며 사용자 모드에서 열 수 있는 legacy
  control device: [`CDO-README.md`](CDO-README.ko-KR.md)

## 빌드

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64 -A x64
cmake --build test\flt\runtime\build_x64 --config Debug
```

## 폐기 가능한 VM 실행

모든 커널 테스트는 커널 디버거를 사용할 수 있고 테스트 서명이 활성화된 폐기 가능
VM에서 실행하십시오. 선택한 테스트에는 다음 절차를 적용합니다.

1. `.sys`, INF, catalog 또는 테스트 서명 인증서와 애플리케이션을 준비합니다.
2. 준비한 INF와 서비스 이름으로 미니필터를 설치하고 로드합니다.
3. 해당 가이드에서 요구하면 Driver Verifier를 활성화합니다.
4. 명시적으로 선택한 폐기 가능 NTFS/ReFS 볼륨에서 애플리케이션을 실행합니다.
5. 서비스와 테스트 인스턴스를 언로드하고 제거합니다.
6. 애플리케이션 성공, 드라이버 언로드, crash/dump 없음 및 게스트의 이전 Verifier
   구성 복원을 확인합니다.

호스트 경로, VM 제품, 자격 증명 및 게스트 준비 루트는 테스트 환경의 매개변수이며
이 테스트에서 고정하지 않습니다.
