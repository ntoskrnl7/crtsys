# NTL 미니필터 런타임 테스트

이 문서는 온보딩 예제가 아니라 VM 통합 fixture입니다. 연산별 형식 callback, 캐시된 file name, 네 가지 Filter Manager context 수명 단위, 형식화된 RAII completion state, 조건부 WhenSafe 처리, 같은 volume에 붙는 이름 있는 filter instance 두 개를 의도적으로 결합합니다.

자동 instance는 개발 altitude `370030.227`을 사용합니다. app은 `370030.228`에 secondary definition을 붙여 형식화된 communication-port 및 shared-ring 검사를 수행하고 create, read, write, rename, cleanup, close를 실행한 뒤 detach합니다. 통신 범위에는 contract discovery와 자동 파생 schema 검증, decode 전 authorization, 동시 async call, 협력적 cancellation, transient/replayable notification, 제한된 reliable-queue quota, 형식화된 stream upload/download 및 batching, ACK replay를 갖는 명시적 session preserve/resume, cross-channel priority, 취소된 notification-wait 정리, connection/session 거부 quota, reliable byte quota, malformed/oversized framing, 잘못된 shared-region range/access/quota token, stale shared-region token이 포함됩니다. stream facade는 임시 client가 반환해도 기반 connection을 유지하는지 확인하기 위해서도 반환됩니다. C++20 build는 method, notification, stream receive에 대해 형식화된 `co_await` call도 실행합니다. driver는 unload 중 context와 operation counter를 기록합니다.

runtime app은 명시적 altitude로 secondary instance를 attach/detach합니다. 별도의 security probe는 administrators security descriptor가 administrator는 허용하고 임시 non-administrator account는 거부하는지 확인합니다. 일반 app은 mismatched schema hash가 일반 traffic 전에 거부되는지도 검증합니다.

소유권 범위는 load된 driver에서 Microsoft sample mechanism 네 가지도 검사합니다.

- transaction context를 만들고 transacted create에 enlist하며 commit-finalize, rollback, destructor counter를 검사합니다.
- NTFS/ReFS instance가 data scanning을 등록하고 읽기 전용 section을 만들어 매핑하며 overwrite conflict를 받고 notification callback에서 section을 닫고 section context를 파괴합니다.
- 생성된 callback data가 동기/비동기 형식화된 file query를 수행하고 directory-notify request를 보류했다가 `async_callback_data_operation`으로 취소해 `STATUS_CANCELLED`를 관찰한 뒤 handle을 해제하고 정상 unload합니다.
- app이 시작한 overlapped directory notification은 형식화된 lower-stack operation-status callback을 요청하고 `STATUS_PENDING`을 관찰하며 읽기 전용 directory-control snapshot을 검증하고 nonpaged request state를 정확히 한 번 파괴한 뒤 notification을 취소하고 정상 unload합니다.

생성 I/O 테스트는 의도적으로 `FltSetCancelCompletion`을 사용하지 않습니다. 이 routine은 미니필터가 게시하는 기존 incoming IRP를 예상하지만, 생성 callback data는 `FltPerformAsynchronousIo`가 제출할 때까지 backing IRP가 없습니다.

create 경로는 flag 없는 normal post와 함께 `on_with_completion<T>()`를 사용합니다. read 경로는 flag 없는 immediate post가 `post_continuation::when_safe`를 반환하는 같은 형식 state를 사용합니다. safe callback은 같은 state를 `IRQL <= APC_LEVEL`에서 받습니다. NTL은 direct/safe 처리 뒤, scheduling 실패 시 또는 drain 중 직접 각 object를 파괴합니다. unload log는 create/observe/destroy count를 보고하므로 VM 실행은 온보딩 example을 stress test로 만들지 않고도 수명 regression을 드러냅니다.

같은 CMake project는 격리된 feature fixture 7개도 빌드합니다.

- I/O buffer 쌍: [`IO-BUFFER-README.md`](IO-BUFFER-README.ko-KR.md)
- NameChanger 쌍: 형식화된 name-provider 등록, create redirect, name generation/normalization, directory graft visibility, hard-link 정보, name-bearing USN/Find-by-SID/cluster FSCTL result: [`NAME-CHANGER-README.md`](NAME-CHANGER-README.ko-KR.md)
- SimRep 쌍: 형식화된 simulated reparse, network-query-open fallback, rename/hard-link destination repair, tunneled-name ownership: [`SIMREP-README.md`](SIMREP-README.ko-KR.md)
- delete 쌍: 형식화된 legacy/extended disposition, create-time delete-on-close, cleanup 확인, 강제 disposition 경쟁, alternate-stream 분류: [`DELETE-README.md`](DELETE-README.ko-KR.md)
- Scanner/AvScan 쌍: post-create cancellation, 보류된 형식 write policy, cleanup rescan, data-scan section, TxF notification: [`SCANNER-README.md`](SCANNER-README.ko-KR.md)
- MetadataManager 쌍: implicit/explicit lock, snapshot, dismount, detach, remount 전반의 volume별 metadata 소유권: [`METADATA-README.md`](METADATA-README.ko-KR.md)
- CDO 쌍: 미니필터 startup/unload 수명이 소유하는 사용자 모드에서 열 수 있는 legacy control device: [`CDO-README.md`](CDO-README.ko-KR.md)

## 빌드

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64 -A x64
cmake --build test\flt\runtime\build_x64 --config Debug
```

## 폐기 가능한 VM 실행

모든 kernel fixture는 kernel debugger를 사용할 수 있는 폐기 가능하고 test signing이 활성화된 VM에서 실행하십시오. 선택한 fixture에 대해 다음을 수행합니다.

1. `.sys`, INF, catalog 또는 test-signing certificate, application을 staging합니다.
2. staged INF와 service name으로 미니필터를 설치·load합니다.
3. 대상 guide가 요구하면 Driver Verifier를 활성화합니다.
4. 명시적으로 선택한 폐기 가능한 NTFS/ReFS volume에서 application을 실행합니다.
5. service와 test instance를 unload·제거합니다.
6. application 성공, driver unload, crash/dump 부재, guest의 이전 Verifier 구성 복원을 확인합니다.

host path, VM product, credential, guest staging root는 테스트 환경의 매개변수이며 이 fixture가 고정하지 않습니다.
