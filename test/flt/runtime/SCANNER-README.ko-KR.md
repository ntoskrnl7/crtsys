# Scanner/AvScan 런타임 fixture

이 격리된 driver/app 쌍은 Microsoft `scanner`, `avscan` 예제가 보여 주는 재사용 가능한 mechanism을 하나의 형식화된 NTL 수명 주기로 구성합니다. production antivirus engine이 아니라 policy 및 소유권 테스트입니다. `\crtsys-flt-scanner-runtime` 아래의 `*.scan` 파일만 관찰하고 테스트 signature `CRTSYS_FOUL`을 처음 4096바이트까지만 검사합니다.

## 파일과 책임

| 파일 | 책임 |
| --- | --- |
| `scanner_shared/scanner_runtime.hpp` | 고정 폭 형식화된 RPC contract, 테스트 이름, verdict, 관찰 가능한 counter |
| `scanner_driver/main.cpp` | create/write/cleanup 정책, data-scan section, pending I/O, context, TxF enlistment |
| `scanner_app/main.cpp` | 사용자 모드 scanner service와 end-to-end 검증 |
| `scanner_driver/crtsys_flt_scanner_runtime_test.inf` | altitude `370030.233`의 개발 instance |

등록 경계는 연산별 형식을 유지합니다.

- `create_callback_data`는 pre/post create를 처리합니다.
- `write_callback_data`는 pre-write를 처리합니다.
- `cleanup_callback_data`는 pre-cleanup을 처리합니다.
- 형식화된 transaction 및 section-context callback이 각 notification을 처리합니다.

연기된 PASSIVE 수준 routine은 Filter Manager work-item ABI 때문에 네이티브 callback data를 받습니다. 이 routine은 곧바로 알려진 `write_callback_data` 형식을 복원합니다. 이는 구현 경계이지 원시 등록을 빠져나가는 통로가 아닙니다.

## 수명 주기

| 연산 | driver 동작 | 사용자에게 보이는 결과 |
| --- | --- | --- |
| 성공한 open | 읽기 전용 data-scan section을 매핑하고 형식화된 요청을 보내며 감염 verdict에는 `try_cancel_file_open()` 호출 | 감염된 기존 파일은 `ERROR_ACCESS_DENIED`로 실패 |
| 쓰기 가능한 open | 형식화된 stream-handle context를 붙이고 TxF가 있으면 형식화된 transaction context enlist | cleanup이 재검사 필요 여부를 앎 |
| non-paging write | PASSIVE 수준으로 연기하고 `try_swap_io_buffers()`로 격리 page에 복사하여 `pending_pre_operation_queue`에 보류하고 verdict 요청 | 정상 page는 하위 stack을 재개하고 감염 page는 해제되며 원래 write는 access denied로 완료 |
| cleanup | data-scan section으로 마지막 파일 내용을 재검사 | 일반 pre-write 검사를 우회한 mapped/paging write를 감지하며 cleanup 자체는 실패하지 않음 |
| transaction 종료 | enlist된 형식화된 transaction context로 commit-finalize 또는 rollback 수신 | 두 TxF 경로와 context 파괴를 관찰 |
| scanner disconnect | caller buffer를 acquire하거나 보류하기 전에 create/write policy 우회 | Microsoft Scanner sample의 service-availability policy와 같은 fail-open bootstrapping 및 test cleanup |

write 경로는 격리된 `swapped_io_buffers` 소유자를 cancel-safe pending queue 안에 유지합니다. allow verdict는 하위 stack을 재개하기 전에 상주 replacement page를 적용합니다. deny, cancellation, disconnect, teardown은 오래된 user buffer가 file system에 보이지 않게 해당 page를 해제합니다.

`FltCreateSectionForDataScan`은 create 호출이 반환하기 전에 section-conflict callback을 호출할 수 있습니다. 따라서 section context는 section 생성 전에 초기화한 atomic abort flag를 소유합니다. 일반 runtime fixture는 conflict 경로를 강제하고, 이 scanner fixture는 성공한 create/map/close 구성이 균형을 이루는지 검사합니다.

## app이 증명하는 사항

app은 연결 전 파일을 준비해 disconnect 상태의 fail-open bootstrapping을 증명한 후 다음을 검증합니다.

- 정상/감염 기존 파일 open
- 허용된 일반 `WriteFile` 하나와 거부된 것 하나
- memory-mapped write 뒤 cleanup 감지
- commit된 TxF write 하나와 rollback된 TxF write 하나
- 전송 실패 없는 형식화된 kernel-to-user 요청
- 균형 잡힌 data-scan section, section context, pending write, stream-handle context, transaction context

통과한 실행은 다음의 결정적인 policy count를 보고합니다.

```text
policy=19/0 open=10/1 write=4/3/1 cleanup=5/1
sections=8/8/8 pending=4/3/1 deferred=4
handle_contexts=5/5 transaction_contexts=2/2
enlistments=2 commits=1 rollbacks=1
```

`open` 뒤 첫 숫자는 scan, 둘째는 거부된 open입니다. `write`는 scan/allow/deny, `cleanup`은 scan/infected, `sections`는 create/map/close, `pending`은 pend/resume/cancel입니다.

## 빌드 및 VM 테스트

```powershell
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_scanner_runtime_test `
           crtsys_flt_scanner_runtime_test_app -- /m:1
```

[폐기 가능한 VM 절차](README.ko-KR.md#disposable-vm-execution)로 driver package와 application을 staging한 뒤 guest에서 실행합니다.

```powershell
$testVolumeRoot = Read-Host 'Disposable test volume root'
.\crtsys_flt_scanner_runtime_test_app.exe $testVolumeRoot
```

WOW64 범위에서는 x64 driver를 유지하고 staged x64 application 대신 x86 application을 실행하십시오. 두 실행 모두 scanner success marker, 정상 driver unload, Verifier/crash event 없음으로 끝나야 합니다.
