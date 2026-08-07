# NTL 삭제 런타임 fixture

이 격리된 driver/app 쌍은 Microsoft `delete` 미니필터 예제의 재사용 가능한 mechanism을 검증합니다. `\crtsys-flt-delete-runtime` 아래의 파일만 관찰하며 개발 altitude `370030.232`를 사용합니다.

fixture는 삭제 요청과 확인된 삭제를 구분합니다.

- `create_parameters::delete_on_close()`는 `FILE_DELETE_ON_CLOSE`를 인식합니다.
- `set_information_parameters::disposition()`은 legacy `FILE_DISPOSITION_INFORMATION` 또는 확장 `FILE_DISPOSITION_INFORMATION_EX`를 검증·복사하여 읽기 전용 형식화된 view로 제공합니다.
- 각 stream context는 성공한 disposition 및 delete-on-close 상태를 추적합니다.
- 겹치는 disposition 연산은 추적 상태를 불확실하게 만드므로, 마지막에 실행된 post callback만 신뢰하지 않고 cleanup도 검사합니다.
- 동기화된 post-cleanup callback은 `try_query_cleanup_deletion(as_post(data))`를 호출합니다. `STATUS_FILE_DELETED`는 삭제를 확인하고, 성공한 standard-information query는 stream이 계속 존재함을 확인합니다.

`FILE_DISPOSITION_ON_CLOSE`는 확장 disposition이 제어하는 delete-on-close 상태를 선택합니다. 런타임 사례는 `FILE_FLAG_DELETE_ON_CLOSE`로 handle을 만들고, `DELETE` 없이 확장 `ON_CLOSE` 요청으로 그 상태를 지워 파일이 유지됨을 증명합니다. 일반 NTFS handle에서 `DELETE | ON_CLOSE`를 새 삭제 요청으로 사용하는 것은 동등하지 않으며 NTFS가 거부합니다.

## 검증 항목

app은 load된 x64 미니필터에서 다음을 모두 증명합니다.

1. legacy delete disposition은 cleanup 전에 설정하고 지울 수 있습니다.
2. 확장 `ON_CLOSE`는 기존 create-time delete-on-close 상태를 지울 수 있으며, 변경하지 않은 `FILE_DELETE_ON_CLOSE` 요청은 파일을 삭제합니다.
3. 확장 POSIX, force-image-section-check, ignore-readonly flag가 파싱되고 읽기 전용 파일이 성공적으로 삭제됩니다.
4. alternate data stream을 삭제하면 base file은 남아 있고 stream 삭제로 보고됩니다. base를 삭제하면 전체 파일 삭제로 보고됩니다.
5. 기존의 두 번째 handle은 삭제가 보류된 동안에도 계속 사용할 수 있으며 마지막 handle이 닫힌 뒤 파일이 사라집니다.
6. 결정적인 두 thread gate가 하나의 stream에서 set/clear disposition 연산을 겹치게 합니다. driver는 경쟁을 기록하고 이후 cleanup에서 최종 삭제를 확인합니다.
7. 형식화된 completion-state 소유권이 정확히 균형을 이루고 filter가 unload되며, 같은 x64 driver에서 x64/x86 app이 모두 통과합니다.

## 빌드

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64_v145
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_delete_runtime_test `
           crtsys_flt_delete_runtime_test_app

cmake -S test\flt\runtime -B test\flt\runtime\build_x86_v145
cmake --build test\flt\runtime\build_x86_v145 --config Debug `
  --target crtsys_flt_delete_runtime_test_app
```

[폐기 가능한 VM 절차](README.ko-KR.md#disposable-vm-execution)를 사용해 driver package와 application을 staging하십시오. 명시적인 폐기 가능 volume root에 대해 app을 실행합니다.

```powershell
$testVolumeRoot = Read-Host 'Disposable test volume root'
.\crtsys_flt_delete_runtime_test_app.exe $testVolumeRoot
```

통과한 app은 다음과 비슷한 counter를 보고합니다.

```text
create_delete_on_close=2 legacy=8 extended=2 delete=7 clear=3
on_close=1 posix=1 force_image=1 ignore_readonly=1
set_success=10 set_failure=0 races=1 race_arrivals=2
cleanup_checks=8 cleanup_present=2 file_deletions=5 stream_deletions=1
completion_states=20/20
NTL delete runtime test PASS
```

구현은 [`delete_driver/main.cpp`](delete_driver/main.cpp), verifier는 [`delete_app/main.cpp`](delete_app/main.cpp), 고정 폭 RPC contract는 [`delete_shared/delete_runtime.hpp`](delete_shared/delete_runtime.hpp)에 있습니다.

fixture가 사용하는 Microsoft contract:

- [FILE_DISPOSITION_INFORMATION_EX](https://learn.microsoft.com/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex)
- [FltQueryInformationFile](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltqueryinformationfile)
- [FILE_INFORMATION_CLASS](https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class)
- [FILE_DISPOSITION_INFO](https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-file_disposition_info)
