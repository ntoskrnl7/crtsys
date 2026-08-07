# NTL 미니필터 테스트

이 디렉터리는 공개 샘플에 넣으면 가독성을 해칠 수 있는 미니필터 API 계약과
런타임 픽스처를 담당합니다.

Microsoft 샘플과 NTL 메커니즘의 저장소 전체 대응 관계는
[`WDK 미니필터 샘플 적용 범위`](WDK-SAMPLE-COVERAGE.ko-KR.md) 매트릭스에
정리되어 있습니다. 대응하는 공개 API, 컴파일 계약 또는 런타임 증거가 바뀐
경우에만 이 매트릭스를 갱신하십시오.

`compile/registration_and_ownership.cpp`는 이름 공급자 등록, 트랜잭션 참여/제거,
데이터 스캔 섹션 설정 및 소유된 콜백 데이터 I/O를 한 파일에서 보여 주는 사용
계약입니다. 소유권 전환을 명확히 드러내도록 의도적으로 드라이버 코드처럼
구성했지만, 필터 관리자 인스턴스를 마운트하지 않고도 일반 x86/x64 매트릭스에서
컴파일하고 링크할 수 있습니다.

이 컴파일 픽스처는 형식화된 콜백 시그니처, 네이티브 `PFLT_*` 콜백 시그니처의
거부, 연속형 등록 API, 컨텍스트 형식, 트랜잭션 참여/제거, 크기가 제한된 이름
출력, 데이터 스캔 설정/정리, 형식화된 취소 및 콜백 데이터 소유권 이전을
검증합니다. 필터 관리자를 시뮬레이션하지 않으므로 다음 동작은 일회용 VM에 실제
미니필터를 로드해 검증해야 합니다.

- 이름 공급자 캐시 및 정규화 동작
- Driver Verifier 아래의 언로드 및 런다운 동작

일반 런타임 픽스처는 KTM commit/rollback 전달, 데이터 스캔 섹션 충돌과 정리,
생성된 동기/비동기 I/O의 취소와 해체를 검증합니다. 실제 overlapped 디렉터리
알림도 발행하고, 형식화된 하위 스택 작업 상태 스냅샷, `STATUS_PENDING`, 요청
상태 파괴, 취소 및 언로드를 확인합니다. 네이티브 x64와 WOW64 클라이언트 모두
x64 드라이버를 대상으로 실행됩니다.

`compile/operation_callback.cpp`는 일반 CMake 드라이버 테스트 매트릭스에
포함됩니다. 작업 태그 콜백 문법, 작업/콜백 형식 불일치 거부, 작업별 매개변수
접근, 읽기 전용 작업 상태 스냅샷, 네이티브 작업 상태 콜백 거부 및 `auto`
매개변수를 사용하는 일반 람다를 검증합니다. 이를 위해 `examples/minifilter`에
테스트 전용 문장을 추가하지 않습니다. 일반 람다 형식도 컴파일할 수 있지만, 현재
C++ 편집기의 자동 완성 엔진이 문맥에 따라 인스턴스화되는 일반 람다 매개변수의
멤버를 안정적으로 나열하지 못하므로 공개 샘플은 명시적인 콜백 데이터 형식을
사용합니다.

`compile/context.cpp`는 형식화된 파일/스트림 컨텍스트 선언, 이동 전용 필터
관리자 참조 소유자, 생성자 인수 전달 및 지원되는 전체 MSVC 도구 집합의
등록 인터페이스를 검사합니다. 런타임 파일 시스템 동작은 일반 WDM 단위 테스트
드라이버와 섞지 않고 미니필터 드라이버/앱 픽스처에서 검증합니다.

`compile/control_device.cpp`는 미니필터가 `fltKernel.h`를 직접 포함하거나 원시
WDM 디스패치 테이블을 설정하지 않고도 형식화된 `ntl::device`를 큐에 추가할 수
있는지 검증합니다.

`compile/abi_win7_provider.cpp`와 `compile/abi_win8_consumer.cpp`는 대상 버전을
가로지르는 링크 계약을 이룹니다. Windows 7 객체는
`sizeof(ntl::flt::driver)`와 `sizeof(ntl::flt::registration)`을 인수로 하는 템플릿
심볼을 정의하고, Windows 8 객체는 자신의 관점에서 계산한 같은 심볼을
요구합니다. 따라서 WDK 버전 게이트가 사전 빌드 공개 라이브러리의 어느 레이아웃을
바꾸더라도 일반 드라이버 테스트 링크가 실패합니다.

`compile/instance.cpp`는 소유 인스턴스 참조, ID 쿼리, 이름/고도 기반 연결
오버로드, 분리 인터페이스 및 필터 전체 인스턴스 열거를 지원되는 모든 도구
집합에서 검증합니다.

`runtime/`은 독립적인 드라이버/앱 통합 픽스처입니다. 다중 인스턴스, 네 가지
컨텍스트, 조건부 WhenSafe, 종료 처리 및 명시적 분리 검증을 온보딩 예제 밖에
두면서도, VM에서 실제 미니필터로 빌드하고 로드할 수 있습니다. 통신 테스트는
잘못된 프로토콜, 할당량, 코루틴, 연결 해제 및 언로드 수명 검사도 담당합니다.
온보딩 샘플은 의도적으로 더 짧게 유지합니다.

`runtime/io_buffer_*`는 매핑/교체 I/O 버퍼를 위한 별도의 필터/앱 쌍입니다. 교체
페이지를 필터 관리자 포트를 통해 연결된 프로세스에 매핑하고, 사용자 모드의 쓰기
전 암호화와 읽기 후 복호화/되쓰기, VAD 무효화, 연결이 끊긴 서비스의 거부,
PASSIVE 지연, Fast I/O 재시도, 언로드/재로드 소유권, 양쪽 보류 방향에서의 활성
시간 초과/연결 해제/해체 취소 및 필터 아래 파일에 실제 암호문이 저장되는지
검증합니다. 자세한 내용은
[`runtime/IO-BUFFER-README.md`](runtime/IO-BUFFER-README.ko-KR.md).

`runtime/name_changer_*`는 별도의 형식화된 이름 공급자 쌍입니다. 존재하지 않는
가상 graft에서 물리적 backing 디렉터리로 열기를 리디렉션하고, backing에 대한 직접
접근을 막으며, 생성된 이름을 변환하고, 부모 디렉터리 열거·하드 링크 쿼리·이름을
포함한 FSCTL 출력을 조정합니다. 검증 앱은 NTFS/ReFS와 x64/WOW64 호출자에서 필터
언로드 전후의 매핑을 입증합니다. 정확한 검증 범위와 파일 시스템별 미지원 작업은
[`runtime/NAME-CHANGER-README.md`](runtime/NAME-CHANGER-README.ko-KR.md).

`runtime/simrep_*`는 격리된 SimRep 쌍입니다. 단계가 형식화된 pre-create 재분석,
network-query-open Fast I/O 대체 경로, 검증된 이름 변경/하드 링크 대상, 하위
인스턴스 재발행 및 터널 이름 완료 상태 소유권을 x64와 WOW64 앱에서 검증합니다.
자세한 내용은
[`runtime/SIMREP-README.md`](runtime/SIMREP-README.ko-KR.md).

`runtime/delete_*`는 격리된 삭제 쌍입니다. 복사된 형식화 레거시/확장 disposition
뷰, 생성 시 delete-on-close, cleanup 후 삭제 확인, 강제로 경쟁시키는 작업,
여러 핸들의 보류 삭제 및 대체 스트림과 전체 파일의 구분을 x64와 WOW64 앱에서
검증합니다. 자세한 내용은
[`runtime/DELETE-README.md`](runtime/DELETE-README.ko-KR.md).

`runtime/scanner_*`는 격리된 Scanner/AvScan 쌍입니다. 형식화된 드라이버-앱 스캔
요청, 성공한 post-create에서의 취소, 격리되고 취소에 안전한 보류 쓰기, 매핑
쓰기 cleanup 재검사, 데이터 스캔 섹션 소유권, 연결 해제 시 허용 동작 및 TxF
commit/rollback 전달을 x64와 WOW64 앱에서 검증합니다. 자세한 내용은
[`runtime/SCANNER-README.md`](runtime/SCANNER-README.ko-KR.md).

`runtime/metadata_*`는 암시적/명시적 볼륨 잠금, 스냅샷 업데이트 보류, 이전
인스턴스 무효화, 성공적인 dismount/detach 및 ReFS 재마운트 전반에서 인스턴스별
메타데이터 소유권을 검증합니다. 자세한 내용은
[`runtime/METADATA-README.md`](runtime/METADATA-README.ko-KR.md).

`runtime/cdo_*`는 미니필터 소유 레거시 제어 장치의 시작과 해체, 사용자 모드
열기, 형식화된 IOCTL, 선택적 언로드 거부, 계속되는 디스패치, cleanup/close 및
다시 열기를 검증합니다. 자세한 내용은
[`runtime/CDO-README.md`](runtime/CDO-README.ko-KR.md).

`cross-bitness/`는 사용자 모드 전용 빌드 픽스처입니다. Win32로 구성한 뒤
`runtime/`에서 생성된 x64 드라이버에 대해 x86 앱을 실행합니다. 공유된
필터 관리자 포트 레코드에는 고정 너비 ID, 크기 및 토큰이 포함되어 있으므로
테스트는 일반 RPC 픽스처와 동일한 교차 비트 계약을 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-cross-bitness-app -Architecture x86 `
  -PlatformToolset v143 -Configuration Debug
```

통합 환경에서는 x64 `crtsys_flt_runtime_test.sys` 드라이버와 x86
`crtsys_flt_runtime_x86_app.exe` 클라이언트를 함께 사용해야 합니다. 이는 32비트
프로세스가 64비트 드라이버를 로드할 수 있다는 뜻이 아닙니다.

`verifier-stress/`는 통신 포트 닫기, 필터 언로드 및 필터 재로드를 반복하는
별도의 사용자 모드 픽스처입니다. 이미 구성된 Driver Verifier 세션에서
Verifier 설정을 변경하지 않고 실행하도록 설계되었습니다.
