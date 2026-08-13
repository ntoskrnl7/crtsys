# NTL NameChanger 런타임 테스트

이 픽스처는 이름 제공자 등록 예제를 로드 가능한 예제로 바꿉니다.
미니 필터 및 검증 앱. 이는 핵심 네임스페이스 접목을 모델로 합니다.
Microsoft의 `filesys/miniFilter/NameChanger` 샘플 계약:

```text
\crtsys-namechanger-visible\graft
                  |
                  +----> \crtsys-namechanger-store\real
```

표시되는 `graft` 디렉터리가 디스크에 존재하지 않습니다. 드라이버가 리디렉션합니다.
해당 경로 아래에 백업 디렉터리에 대한 요청을 미리 생성하고 직접 거부합니다.
백업 매핑을 통해 열리고 열린 개체에 대해 표시 가능한 이름이 생성됩니다.
이식편을 통해 눈에 보이는 상위 항목을 열거하면서 `graft`를 주입합니다.
백업 부모를 열거하는 동안 `real`를 숨깁니다. 매핑 문자열은 다음에서 비롯됩니다.
설치된 서비스의 `Parameters` 키 및 연결된 각 NTFS/ReFS 볼륨
유형화된 `instance_context`를 소유하고 있습니다.

구현은 공개 형식의 표면을 통해 의도적으로 작성되었습니다.

- `create_callback_data` 및 `create_parameters`는 리디렉션 생성을 수행합니다.
- 현재 아래의 `name_generation_request::try_query_lower_name()` 쿼리
  재귀 없는 공급자;
- `instance::try_get(mapping_context)`는 인스턴스별 매핑 상태를 제공합니다.
  등록 콜백;
- `name_normalization_output`는 구성 요소 출력을 제한합니다.
- 타입이 지정된 스트림 핸들 컨텍스트는 합성 열거 여부를 기록합니다.
  항목이 이미 반환되었습니다.
- 타입이 지정된 완료 상태는 쿼리 정보에 대해 `prepared_output_buffer`를 전달합니다.
  디렉토리 열거 및 FSCTL 출력을 안전한 포스트 콜백으로 출력합니다.
- 쿼리 정보, USN 및 디렉터리 결과는 다음을 통해서만 액세스됩니다.
  드라이버 소유 MDL 및 사후 작업이 없는 준비된 범위 방문자
  `FltLockUserBuffer`.

쿼리 정보 출력은 의도적으로 동일한 준비된 방문자를 사용합니다.
`swapped_io_buffers`보다. 쿼리 사전 콜백은 `APC_LEVEL`에서 실행될 수 있지만
교체 버퍼 할당에는 `PASSIVE_LEVEL`가 필요합니다. 그것을 치료
작업 결과가 잘못 유효하게 바뀌므로 할당이 실패합니다.
`NtQueryInformationFile`는 `STATUS_INVALID_DEVICE_STATE`에 요청합니다.

픽스처에는 원시 WDK 콜백 서명이 없습니다. 네이티브 필터 관리자
호출은 작업 자체가 없는 콜백 본문 내부에 계속 나타납니다.
NTL 소유권 래퍼(예: 구성 요소의 상위 항목 열기/쿼리)
정규화.

## 보장되는 계약

앱은 두 계층으로 구성됩니다. `name_changer_app/main.cpp`는 테스트 픽스처의
수명 주기를 관리하고 열기, 읽기, 쓰기 및 이름 공급자 동작을 검증합니다.
`name_changer_app/directory_tests.cpp`가 `NtQueryDirectoryFile`를 직접 호출합니다.
테스트에서는 정보 클래스와 플래그를 선택할 수 있습니다.
`std::filesystem::directory_iterator`는 노출되지 않습니다.

앱은 보이는 상위 디렉터리와 물리적 백업 디렉터리만 생성합니다.
필터를 로드하기 전에 지원 항목을 캡처한 후 다음을 확인합니다.

1. 존재하지 않는 가시적 이식편을 통해 뒷받침 페이로드를 읽을 수 있습니다.
2. 필터가 활성화되어 있는 동안에는 백킹 경로를 직접 열 수 없습니다.
3. 타입이 지정된 생성 후 이름 쿼리는 등록된 공급자를 호출하고 보고합니다.
   백업 이름이 아닌 눈에 보이는 이름;
4. 가시적 부모 열거형은 정확히 하나의 `graft`, 지원 부모를 주입합니다.
   열거형은 `real`를 노출하지 않으며 하위 열거형은 `graft`를 통해 노출되지 않습니다.
   지원 페이로드를 반환합니다.
5. 모든 열거 검사가 요청됩니다.
   `FileDirectoryInformation`, `FileFullDirectoryInformation`,
   `FileBothDirectoryInformation`, `FileNamesInformation`,
   `FileIdBothDirectoryInformation` 및
   `FileIdFullDirectoryInformation`, 플러스
   `FileIdExtdDirectoryInformation`, `FileIdExtdBothDirectoryInformation`,
   `FileId64ExtdDirectoryInformation` 및
   `FileId64ExtdBothDirectoryInformation`. 대상은 필터가 보기 전에 정보 class를
   거부할 수 있으며, 대상이 받아들인 모든 class는 전체 검사를 통과해야 합니다.
   Windows 11 빌드 22000은 NTFS와 ReFS 3.7 모두에서 10개 중 8개를 받아들이고 두
   `FileId64Extd*` class를 거부합니다. Windows 11 25H2 빌드 26200 guest는 x64 및
   WOW64 client를 사용하는 NTFS와 ReFS 3.14에서 10개 모두를 받아들이고 E2E로
   검증합니다. 동일한 4방향 matrix가
   파일 시스템 필터를 포함한 표준 드라이버 검증 프로그램을 통과합니다.
   검증. 앱은 거부된 모든 수업을 보고하고 내보냅니다.
   `file_id_64_extd_directory_classes_e2e_verified=2 supported=2 total=2`
   두 클래스 모두 파일 시스템 지원 검사를 완료하면;
6. 삽입된 항목은 지원 항목의 안정적인 타임스탬프, 크기,
   속성, EA 크기 및 파일 ID를 삭제하는 동시에 백업 짧은 이름을 지웁니다.
   `LastAccessTime`는 의도적으로 별도의 쿼리 간에 비교되지 않습니다.
   쿼리 자체가 업데이트할 수 있기 때문입니다.
7. 정확한 와일드카드 패턴(`g*`, `?raft` 및 `*aft`)에는 접목이 포함됩니다.
   일치하지 않는 패턴은 이를 제외하고 첫 번째 요청의 패턴은 그대로 유지됩니다.
   Windows가 `FileName`를 생략하는 연속 요청에 효과적입니다.
8. 작은 버퍼는 중복 없이 페이지 매김, `SL_RETURN_SINGLE_ENTRY`
   올바르게 종료되고 `SL_RESTART_SCAN`는 이식편을 한 번 표시합니다.
   다시;
9. `real`를 숨기면 첫 번째, 중간,
   마지막 또는 버퍼에 반환된 유일한 레코드입니다.
10. 보호 바이트, 레코드 범위, 이름 길이, 정렬 및
    `NextEntryOffset` 체인은 계속 유효합니다.
11. `FileNameInformation`, `FileNormalizedNameInformation` 및
    `FileAllInformation`는 가시 경로를 노출합니다. 대체 이름 출력은 다음과 같습니다.
    대상 파일 시스템이 이를 제공할 때 확인됩니다.
12. 보이는 접목 아래의 이름 바꾸기 및 하드 링크 대상이 번역됩니다.
    파일 ID를 유지하면서 백업 디렉터리로 재발행됩니다.
    `FileHardLinkInformation`는 표시되는 상위 ID 및 `graft`를 보고합니다.
    물리적 `real` 링크 대신 구성요소;
13. 비동기 디렉터리 변경 알림은 보이는 하위 이름을 보고합니다.
    생성 및 이름 바꾸기 작업용
14. 접목을 통한 쓰기는 백업 디렉토리에 도달합니다.
15. `FSCTL_READ_FILE_USN_DATA`는 지원 구성 요소와 상위 파일을 다시 작성합니다.
    8개 정렬을 순환하는 동안 512개의 연속 호출에 대한 ID
    사용자 버퍼 오프셋;
16. `FSCTL_ENUM_USN_DATA` 및 `FSCTL_READ_USN_JOURNAL`는 USN v2와 `FSCTL_READ_USN_JOURNAL`를 모두 다시 작성합니다.
    8바이트 연속 접두사를 포함한 v3 레코드
17. `FSCTL_FIND_FILES_BY_SID`는 공통 조상 결과를 변환하고
    `graft\payload.txt` 표시되는 상위 항목이 쿼리될 때 억제합니다.
    지원하는 상위 항목이 쿼리되고 보존될 때 `real\payload.txt`
    64KiB 및 128바이트 출력 버퍼 모두에 걸친 연속 상태
18. `FSCTL_LOOKUP_STREAM_FROM_CLUSTER`는 가시적 스트림 경로를 반환하고
    연결된 항목 오프셋과 필요한 버퍼 크기를 다시 계산합니다.
19. 대신 일반 생성 경로를 통해 네트워크 쿼리 열기 재시도
    네임스페이스 번역 우회
20. 언로드하면 실제 파일이 표시되고 보이는 디렉터리가 없음을 증명합니다.
    실수로 생성되었습니다.

드라이버는 결정적 합성 추가/숨기기/레코드 체인 테스트도 실행합니다.
로드될 때마다 두 `FileId64Extd*` 레이아웃 모두에 대해. 시작을 거부합니다
둘 다 통과하고 앱에서 요구하지 않는 한
`synthetic_file_id_64_layouts=2/2`. 이는 버퍼 변환 코드를 다룹니다.
해당 정보 클래스를 거부하는 대상에서도 마찬가지입니다. 보완하지만
Windows 11 25H2 파일 시스템 지원 E2E 결과를 대체하지 않습니다.

NTFS/ReFS 매트릭스는 이식성 및 소유권 테스트이지,
ReFS는 NTFS에서 사용할 수 없는 작업을 활성화합니다. 두 파일 시스템 모두
Windows 11 빌드 22000의 동일한 8개 클래스와 Windows 11의 동일한 10개 클래스Windows 11 빌드 26200. 두 번째 파일 시스템이 가능하므로 ReFS는 여전히 가치가 있습니다.
다양한 결과 버퍼, 메타데이터, 필터 관리자 경로를 실행하고
NTFS 전용 실행으로 숨겨진 가정을 노출합니다.

관찰된 파일 시스템 분할은 의도적인 것입니다.

- NTFS는 `FSCTL_ENUM_USN_DATA`를 지원하고 전달합니다.
  `FSCTL_READ_USN_JOURNAL`, `FSCTL_FIND_FILES_BY_SID` 및
  `FSCTL_LOOKUP_STREAM_FROM_CLUSTER`. SID별 찾기에는 할당량 추적이 필요합니다.
  NTFS는 할당량 인덱스를 사용하기 때문에 전용 NTFS 테스트 볼륨
  소유자-SID 조회.
- ReFS 3.7 및 3.14는 `FSCTL_READ_USN_JOURNAL`을 지원하고 테스트를 통과하지만,
  시험한 게스트에서는 enum-USN, Find-by-SID 및 클러스터 조회 호출에
  `ERROR_INVALID_FUNCTION`을 반환합니다. 앱은 이 결과를 `UNSUPPORTED`로
  보고하며, 파일 시스템이 거부한 작업에는 재작성 횟수를 요구하지 않습니다.

이는 다음과 같은 주장보다는 집중된 NameChanger 적용 범위로 남아 있습니다.
모든 파일 시스템은 모든 API를 허용합니다. 현재 SID별 찾기 구현
하나의 내부 실제 매핑 배치를 최대 16MiB까지 늘린 다음 페이지를 매깁니다.
호출자의 스트림 핸들 컨텍스트를 통해 레코드를 변환합니다. 결과 세트
그 범위보다 크면 명시적인 후속 조치 범위가 유지됩니다. 마이크로소프트
샘플 자체에는 `FSCTL_TXFS_LIST_TRANSACTION_LOCKED_FILES`가 향후 작업으로 나열되어 있습니다.
따라서 이 픽스쳐는 구현된 샘플 동작으로 표시되지 않습니다.

## 빌드 및 실행

기존 미니필터 런타임 프로젝트를 빌드합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-runtime -Architecture x64 `
  -PlatformToolset v145 -Configuration Debug
```

일회용 테스트 서명 지원 VM에만 설치하고 테스트하세요. 무대와 사인
`crtsys_flt_name_changer_runtime_test.sys`, 해당 INF 및 생성된 카탈로그
INF를 설치하고 드라이버에 대해 Driver Verifier를 활성화한 후 다음을 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Run-FltNameChangerRuntimeTests.ps1 `
  -AppPath test\flt\runtime\build_x64_v145\Debug\crtsys_flt_name_changer_runtime_test_app.exe `
  -VolumeRoot C:\ `
  -RequireVerifier
```

선택한 볼륨은 NTFS 또는 ReFS여야 합니다. 주자는 고도가 필요합니다.
테스트 서명 및 설치된 서비스를 확인하고 선택적으로 드라이버를 확인합니다.
검증자는 대상을 지정하고 앱이 필터를 언로드된 상태로 유지하는지 확인합니다.
NTFS SID별 찾기를 실행하기 전에 일회용 컴퓨터에서 할당량 추적을 한 번 활성화합니다.
게스트 테스트 볼륨(예: `fsutil quota track D:`) 그거 적용하지 마세요
호스트 볼륨에 대한 설정 명령입니다.
