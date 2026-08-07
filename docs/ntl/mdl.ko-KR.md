# NTL MDL 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/mdl`](../../include/ntl/mdl)

`ntl::mdl`은 `IoAllocateMdl`이 할당한 MDL을 소유합니다. 일반 WDK 메모리 관리 primitive를 명시적으로 호출하면서도 MDL 수명을 C++ 범위에 연결해야 할 때 유용합니다.

## 예제

```cpp
auto mdl = ntl::mdl::allocate(buffer, buffer_size);
if (!mdl) {
  return mdl.status();
}

mdl->build_for_nonpaged_pool();

auto system_address = mdl->system_address();
if (!system_address) {
  return system_address.status();
}

use_mapped_buffer(*system_address, mdl->byte_count());
```

pageable 버퍼 또는 사용자 버퍼는 먼저 페이지를 잠그십시오.

```cpp
auto lock_status = mdl->probe_and_lock_pages(KernelMode, IoModifyAccess);
if (!lock_status) {
  return lock_status.status();
}

// The destructor unlocks locked pages before freeing the MDL.
```

## API 요약

- `ntl::mdl::allocate(virtual_address, length, secondary_buffer, charge_quota, irp)`
- `get()`
- `operator->()`
- `release()`
- `reset(native_mdl)`
- `build_for_nonpaged_pool()`
- `probe_and_lock_pages(access_mode, operation)`
- `unlock_pages()`
- `system_address(priority)`
- `byte_count()`
- `byte_offset()`
- `virtual_address()`

`release()`는 네이티브 `PMDL`을 호출자에게 넘깁니다. `release()` 이후에는 호출자가 올바른 WDK primitive로 이를 해제해야 합니다.

## IRQL

기반 WDK primitive의 계약을 따르십시오.

- MDL 할당/해제와 nonpaged pool 설명은 제어 경로에서 사용하기 적합합니다.
- `MmProbeAndLockPages`에는 예외 및 access-mode 동작이 있으므로 해당 WDK 계약이 유효한 경로에서만 사용하십시오.
- 매핑과 페이지 잠금은 검토된 제어 경로 작업으로 취급해야 합니다.

## 드라이버 테스트 범위

드라이버 테스트 모음은 다음을 검사합니다.

- nonpaged 스택 저장소용 MDL 할당
- `MmBuildMdlForNonPagedPool`
- system address 조회
- move 소유권
- release/수동 해제
- 빈 MDL 오류 경로
