# NTL SimRep 런타임 픽스처

이 독립된 드라이버/앱 쌍은 Microsoft SimRep 미니필터 샘플에서 재사용 가능한
메커니즘을 검증합니다. 개발 고도 `370030.231`에서 보이는 볼륨 상대 경로
`\crtsys-flt-simrep-visible`를 `\crtsys-flt-simrep-backing`으로 매핑합니다.

드라이버는 타입이 지정된 NTL 콜백 경계만 사용합니다.

- `try_complete_reparse(as_pre(create), absolute_name, absolute)`는
  `FILE_OBJECT` 이름을 바꾸고 `STATUS_REPARSE`/`IO_REPARSE`로 완료합니다.
- `network_query_open_parameters`는 페이징 파일 및 ID로 여는 요청을 식별하며,
  매핑된 Fast I/O 요청에는 `pre_result::disallow_fast_io`를 반환합니다.
- `set_information_parameters::destination()`는 이름 바꾸기/링크 레이아웃을
  검증하고, `try_query_destination_name()`은 대상을 확인하며,
  `try_reissue_destination()`은 수정한 요청을 인스턴스 아래로 보냅니다.
- 타입이 지정된 create 완료 상태는 정규화한 작업 전 `name_information`을 보관하고,
  작업 후 콜백은 `try_get_tunneled_name()`을 호출합니다.

앱은 이름 제공자가 아닌 이 테스트 필터 아래에서 대상 이름 확인이 부모 디렉터리를
확인할 수 있도록 빈 물리적 표시 디렉터리만 만듭니다. 명시적인 일회성 RPC는 바로
그 루트의 생성 또는 제거에 한해서만 리디렉션을 우회합니다. 모든 하위 항목 열기는
계속 매핑됩니다.

## 검증 항목

앱은 미니필터가 로드된 상태에서 다음을 모두 증명합니다.

1. 보이는 경로를 통한 읽기 및 생성은 백킹 경로에 도달합니다.
2. `NtQueryFullAttributesFile`는 `IRP_MJ_NETWORK_QUERY_OPEN`에 도달하고,
   매핑된 Fast I/O는 거부되며, 다시 발급한 느린 create는 재파싱됩니다.
3. `MoveFileExW` 및 `CreateHardLinkW`의 대상은 확인·변환되어 백킹
   디렉터리에 다시 발급됩니다.
4. 앱은 8.3 별칭이 있는 긴 이름을 만든 뒤 짧은 이름으로 삭제하고, 같은 짧은
   이름을 다시 만듭니다. `FltGetTunneledName`은 구문 분석한 최종 구성 요소가
   `Tunneled Long Name.tmp`로 복원된 null이 아닌 소유자를 반환해야 하며,
   모든 타입이 지정된 완료 상태는 정확히 한 번 소멸해야 합니다.
5. 같은 x64 드라이버는 x64 및 x86 앱 모두에서 통과하고 언로드되며, 매핑된
   테스트 트리를 남기지 않습니다.

이 픽스처는 제한된 SimRep 네임스페이스 변환을 다룹니다. 일반적인 네임스페이스
가상화에는 디렉터리 열거, 알림, 이름 제공자 콜백, 쿼리 정보, FSCTL 결과 재작성을
포괄하는 더 넓은 NameChanger 기능을 사용합니다.

## 빌드

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64_v145
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_simrep_runtime_test `
           crtsys_flt_simrep_runtime_test_app

cmake -S test\flt\runtime -B test\flt\runtime\build_x86_v145
cmake --build test\flt\runtime\build_x86_v145 --config Debug `
  --target crtsys_flt_simrep_runtime_test_app
```

[일회용 VM 워크플로](README.ko-KR.md#disposable-vm-execution)를 사용해 드라이버
패키지와 애플리케이션을 준비합니다. 명시한 일회용 볼륨 루트에서 앱을 실행하세요.

```powershell
$testVolumeRoot = Read-Host 'Disposable test volume root'
.\crtsys_flt_simrep_runtime_test_app.exe $testVolumeRoot
```

성공한 앱은 다음과 비슷한 카운터를 보고합니다.

```text
reparses=5 network_disallowed=1 destination_queries=2
renames_reissued=1 links_reissued=1 tunnel_successes=4
tunnel_names_found=1 tunnel_names_verified=1 tunnel_states=4/4
NTL SimRep runtime test PASS
```

구현은 [`simrep_driver/main.cpp`](simrep_driver/main.cpp)에, 검증기는
[`simrep_app/main.cpp`](simrep_app/main.cpp)에 있으며, 고정 폭 RPC 계약은
[`simrep_shared/simrep_runtime.hpp`](simrep_shared/simrep_runtime.hpp)에 있습니다.

픽스처가 사용하는 Microsoft 계약:

- [작업 전 콜백에서 Fast I/O 거부](https://learn.microsoft.com/windows-hardware/drivers/ifs/disallowing-a-fast-i-o-operation-in-a-preoperation-callback-routine)
- [FltGetDestinationFileNameInformation](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltgetdestinationfilenameinformation)
- [FltSetInformationFile](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltsetinformationfile)
- [FltGetTunneledName](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltgettunneledname)
