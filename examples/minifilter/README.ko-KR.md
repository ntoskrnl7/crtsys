# NTL minifilter 예제

[English](./README.md)

이 catalog는 의도적으로 두 계층으로 나뉩니다.

- **예제**는 드라이버 진입점부터 앱까지 한 가지 드라이버 개념을 읽기 쉽게
  보여줍니다.
- **runtime fixture**는 WDK 예제 coverage를 문서화하는 데 필요한 대규모 호환성,
  실패 경로, 파일 시스템, WOW64 및 Driver Verifier matrix를 보존합니다.

작은 예제도 설명하는 동작에 대해서는 완결되어 있습니다. 훨씬 큰 Microsoft 예제의
제품용 구현인 것처럼 암묵적으로 제시하지는 않습니다.

저장소 전체의 기능 대응 관계와 검증 현황은
[WDK minifilter 예제 coverage matrix](../../test/flt/WDK-SAMPLE-COVERAGE.ko-KR.md)에
정리되어 있습니다.

| 예제 | 대응 WDK 예제 | 핵심 내용 |
| --- | --- | --- |
| [`basic`](./basic) | NullFilter / PassThrough 기반 | 형식화된 create/read/write/cleanup callback, 정규화된 이름, stream context 및 registration 수명 |
| [`control-device`](./control-device) | CDO | minifilter 수명에 속하는 legacy control device, 형식화된 IOCTL dispatch 및 unload 거부 |
| [`communication`](./communication) | Scanner/MiniSpy 통신 기반 | Filter Manager port, 형식화된 RPC, callback, 알림, stream 및 등록된 공유 메모리 ring |
| [`operation-log`](./operation-log) | MiniSpy | 형식화된 I/O callback, 열린 파일별 상태, 크기가 제한된 record queue 및 형식화된 사용자 모드 drain |
| [`swap-buffers`](./swap-buffers) | SwapBuffers | `.ntlxor` 파일에 대한 안전한 pre-write 입력 교체와 post-read 출력 변환/copy-back |
| [`volume-metadata`](./volume-metadata) | MetadataManager | lock, unlock, snapshot, PnP, shutdown 및 teardown 경로 전반의 volume별 metadata 소유권 |

처음에는 `basic`, `control-device`, `communication`, `operation-log`,
`swap-buffers`, `volume-metadata` 순서로 읽는 것을 권장합니다.

## 전체 WDK 방식 구현의 위치

| Microsoft 예제 계열 | 읽기 쉬운 시작점 | 전체 구현 및 검증 |
| --- | --- | --- |
| CDO | [`control-device`](./control-device) | [`CDO-README.ko-KR.md`](../../test/flt/runtime/CDO-README.ko-KR.md) |
| MiniSpy | [`operation-log`](./operation-log) | [`test/flt/runtime`](../../test/flt/runtime) |
| SwapBuffers | [`swap-buffers`](./swap-buffers) | [`IO-BUFFER-README.ko-KR.md`](../../test/flt/runtime/IO-BUFFER-README.ko-KR.md) |
| MetadataManager | [`volume-metadata`](./volume-metadata) | [`METADATA-README.ko-KR.md`](../../test/flt/runtime/METADATA-README.ko-KR.md) |
| Scanner / AvScan | 위의 communication 및 buffer 예제 | [`SCANNER-README.ko-KR.md`](../../test/flt/runtime/SCANNER-README.ko-KR.md) |
| SimRep | 기본 가이드의 형식화된 name API | [`SIMREP-README.ko-KR.md`](../../test/flt/runtime/SIMREP-README.ko-KR.md) |
| NameChanger | 기본 가이드의 형식화된 name API | [`NAME-CHANGER-README.ko-KR.md`](../../test/flt/runtime/NAME-CHANGER-README.ko-KR.md) |
| Delete | 기본 가이드의 형식화된 set-information API | [`DELETE-README.ko-KR.md`](../../test/flt/runtime/DELETE-README.ko-KR.md) |

NameChanger는 오해를 부르는 200줄짜리 예제로 축소하지 않고 의도적으로 커버리지
픽스처로 유지합니다. 이 예제의 계약은 생성 리디렉션, 이름 생성 및 정규화,
디렉터리 열거, 정보 조회, 이름 바꾸기/하드 링크 대상, 알림 및 이름을 포함하는
FSCTL 결과의 조합입니다. 이 기능 대부분을
제거하면 코드는 짧아지지만 NameChanger의 의미를 더 이상 보여주지 못합니다.
연결된 픽스처는 책임을 여러 소스 파일로 분리하고 NTFS와 ReFS에서 모두
검증합니다.

각 예제에는 독립된 CMake project, 수동으로 작성한 Visual Studio `.sln`과
driver/app `.vcxproj`, driver, application, INF, service name, instance name 및
개발용 altitude가 있습니다. altitude 값은 예시일 뿐이며 제품 minifilter에는
Microsoft가 할당한 altitude가 필요합니다.
