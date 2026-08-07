# WDK 미니필터 샘플 적용 범위

이 문서는 `Windows-driver-samples/filesys/miniFilter`가 보여 주는 재사용 가능한
Filter Manager 메커니즘을 NTL API 및 저장소 검증에 대응시킵니다.

적용된다고 해서 NTL이 Microsoft 샘플을 복사하거나 모든 구현 세부 사항을 재현한다는
뜻은 아닙니다. 형식화된 공개 API, 컴파일 타임 계약, 문서, 그리고 런타임 동작이
중요할 때 관찰 가능한 로드 미니필터 테스트가 있으면 해당 메커니즘을 다룬 것입니다.
파일 시스템 또는 Windows 버전 지원은 NTL 지원과 분리하여 보고합니다.

## 적용 범위 매트릭스

| Microsoft 샘플 | 재사용 가능한 메커니즘 | NTL 적용 범위 | 주요 근거 |
| --- | --- | --- | --- |
| `nullFilter` | 등록, 시작, 언로드 | 지원 | `examples/minifilter/basic` 및 VM 로드/언로드 픽스처 |
| `passThrough` | 형식화된 작업 전/후 콜백, 작업 상태 콜백 | 지원 | 작업 컴파일 계약 및 일반 런타임 픽스처 |
| `ctx` | 인스턴스, 파일, 스트림, 스트림 핸들, 트랜잭션 컨텍스트 | 지원 | 컨텍스트 컴파일 계약 및 다중 인스턴스/트랜잭션 수명 테스트 |
| `cancelSafe` | 보류 작업 전 I/O, 취소, 작업자 완료, teardown | NTL 보류 큐 추상화로 지원 | `pending_pre_operation_queue` 컴파일 및 런타임 테스트 |
| `swapBuffers` | I/O 및 제어 경로용 대체 버퍼와 MDL | 지원 | `examples/minifilter/swap-buffers` 및 I/O 버퍼 런타임 픽스처 |
| `scanner` | 통신 포트 스캔과 보류된 정책 결정 | 지원 | scanner 런타임 드라이버/앱 픽스처 |
| `avscan` | 트랜잭션 인식 스캔과 데이터 스캔 섹션 | 재사용 가능한 수명 주기를 지원 | scanner 조합 및 트랜잭션/데이터 스캔 런타임 테스트 |
| `change` | commit/rollback 전파를 이용한 트랜잭션 dirty 추적 | 지원 | 트랜잭션 enlistment, commit, rollback, 정리 테스트 |
| `delete` | close 시 삭제, disposition 변형, 경쟁, 스트림 삭제 | 지원 | delete 런타임 드라이버/앱 픽스처 |
| `minispy` | 광범위한 작업 로깅과 제한된 레코드 전달 | 지원 | `examples/minifilter/operation-log` 및 MiniSpy 런타임 테스트 |
| `MetadataManager` | 볼륨별 메타데이터, 잠금, 분리, PnP, 스냅샷 | 재사용 가능한 볼륨 수명 주기를 지원 | `examples/minifilter/volume-metadata` 및 Verifier 기반 메타데이터 테스트 |
| `NameChanger` | 네임스페이스 grafting, 이름 공급자, 열거, 알림, 쿼리/설정/FSCTL 재작성 | NTL이 구현한 Microsoft 샘플 기능 범위를 지원 | x64/WOW64 클라이언트로 NTFS/ReFS에서 실행한 NameChanger 런타임 픽스처 |
| `simrep` | 모의 reparse, fallback, 대상 이름, tunneled 이름 | 지원 | SimRep 런타임 드라이버/앱 픽스처 |
| `cdo` | 미니필터와 병행하는 레거시 제어 장치 | 지원 | `examples/minifilter/control-device` 및 CDO 런타임 테스트 |

자세한 런타임 계약, 파일 시스템별 결과, 명령은 `test/flt/runtime` 아래 각 픽스처의
README에 있습니다.

## 공통 검증 게이트

행에 해당할 때 다음 게이트를 적용합니다.

1. x64 및 x86을 `/W4 /WX`로 컴파일합니다.
2. 안전하지 않은 소유권 전환과 원시 콜백 대체에는 음성 컴파일 단언이 있습니다.
3. 로드한 x64 드라이버는 일회용 Windows VM에서 실행합니다.
4. 고정 레이아웃 사용자/커널 계약은 x64 드라이버에 x86 앱을 연결해 실행합니다.
5. 작업 콜백보다 오래 살 수 있는 상태는 취소, detach, disconnect 또는 unload로
   검증합니다.
6. 런타임 테스트는 관찰 가능한 단언을 수행하며, 디버그 출력만으로 검증하지 않습니다.
7. 기본 파일 시스템이 작업을 구현하는 경우, 파일 시스템 민감 동작은 NTFS와 ReFS에서
   검증합니다.

## 범위와 한계

- 지원되는 행은 Microsoft 샘플과 소스 수준에서 같다는 뜻이 아니라 재사용 가능한
  메커니즘을 지원한다는 뜻입니다.
- TxF 의존 시나리오는 테스트 환경의 트랜잭션 지원에 따라 계속 조건부입니다.
- 결정론적인 이동식 장치 하드웨어가 필요한 PnP query/cancel/surprise 경로는
  반복 가능한 런타임 하네스가 없을 때 컴파일 범위로만 다룹니다.
- NameChanger는 지원하지 않는 정보 클래스 및 파일 시스템이 거부한 FSCTL을
  명시적으로 유지합니다. 파일 시스템별 작업 수락 여부는 런타임 매트릭스에 남습니다.
- Microsoft 샘플이 향후 작업으로 열거한 동작은 NTL 적용 범위 요구 사항으로
  계산하지 않습니다.

해당 공개 API, 컴파일 계약 또는 런타임 근거가 바뀔 때만 이 매트릭스를 갱신하세요.
상세 카운터, 게스트 빌드 번호, 절차별 테스트 결과는 이 저장소 전체 요약이 아니라
픽스처별 문서에 보관합니다.
