# NTL I/O 버퍼 매핑 및 미니필터 스와핑

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더:

- [`include/ntl/ipc/mapped_buffer`](../../include/ntl/ipc/mapped_buffer)
- [`include/ntl/ipc/io_buffer`](../../include/ntl/ipc/io_buffer)
- [`include/ntl/flt/io_buffer`](../../include/ntl/flt/io_buffer)
- [`include/ntl/flt/swapped_io_buffer`](../../include/ntl/flt/swapped_io_buffer)
- [`include/ntl/flt/pending_io`](../../include/ntl/flt/pending_io)
- [`include/ntl/flt/deferred_io`](../../include/ntl/flt/deferred_io)

API는 두 가지 작업을 구분합니다.

- **매핑**은 연결된 사용자 프로세스 하나에서 버퍼를 볼 수 있게 한다는 뜻입니다.
- **교체**는 미니필터가 작업의 IOPB에 페이지 격리 교체 저장소를 설치한다는
  뜻입니다.

매핑, 교체, Filter Manager 통신 및 보류 처리는 의도적으로 분리되어 있습니다.
결합된 `pend_and_send` 작업은 없으며 호출자가 `from_direct_io`,
`from_buffered_io`, `from_neither_io` 도우미를 선택할 일도 없습니다.

## 프로세스 연결 및 와이어 식별

WDM 파일/IOCTL 연결에서는 해당 파일의 create 콜백에서 연결할 서비스 프로세스를
캡처합니다.

```cpp
auto connection = ntl::ipc::process_connection::try_capture_current_process({
    .max_mappings = 256,
    .max_mapped_bytes = 512u * 1024u * 1024u,
});
if (!connection)
  return connection.status();
```

캡처, 매핑, 매핑 해제는 `PASSIVE_LEVEL` 작업입니다. 각 매핑은 프로세스 참조를
유지합니다. 연결은 단조 증가하는 세대와 매핑 ID를 할당하고, 닫기가 시작된 뒤에는
새 매핑을 거부하며, 매핑·되쓰기·소유 리소스 종료 처리를 동기적으로 수행합니다.
대상 프로세스가 이미 종료 중이면 backing 페이지를 해제하기 전에 주소 공간이
해체될 때까지 기다립니다. 연결 해제는 사용자 VAD를 만들었지만 아직 레지스트리에
삽입하지 않은 매핑 시도도 기다립니다.

세션 핸드셰이크에서 `connection->generation()`를 보냅니다. 와이어 설명자
고정 너비 필드를 사용하며 x86/x64에서 안정적입니다.

```cpp
struct mapped_buffer_descriptor {
  std::uint64_t mapping_id;
  std::uint64_t generation;
  std::uint64_t mapped_address;
  std::uint64_t length;
  std::uint32_t access;
  std::uint32_t reserved;
};
```

드라이버는 어느 프로세스에 연결 설정을 허용할지 인증해야 합니다. 신뢰할 수 없는
메시지로 나중에 전달받은 PID는 연결 ID가 아닙니다. 포트/파일 연결 해제, 서비스
종료, 인스턴스 해체, 언로드 시 `connection->close()`를 호출합니다.

NTL Filter Manager 통신 포트에서는 이 과정이 자동입니다. 포트는 연결을 수락하기
전에 접속 프로세스를 캡처하고 참조합니다.
`communication_connection::mapping_process()`는 공유 매핑 레지스트리에 대한 복사
가능 핸들을 반환합니다. 포트별 VAD 할당량은 다음과 같이 구성합니다.

```cpp
ntl::ipc::process_connection_limits mapping_limits{
    .max_mappings = 32,
    .max_mapped_bytes = 64u * 1024u * 1024u,
};
ntl::flt::communication_port_options port_options;
port_options.process_mapping_limits(mapping_limits);

server.on_connect([](ntl::flt::communication_connection &client) {
  auto process = client.mapping_process();
  return process.accepts_mappings() ? STATUS_SUCCESS
                                    : STATUS_INVALID_DEVICE_STATE;
});
```

Filter Manager가 연결을 끊을 때는 먼저 새 포트 작업을 차단하고, NTL이 해당
프로세스의 모든 매핑을 닫은 다음에야 애플리케이션의 disconnect observer를
호출합니다. 따라서 복사된 연결 객체로 연결 해제 후 매핑을 되살릴 수 없으며,
호출자가 나중 시점의 PID를 다시 조회하는 일도 없습니다.

## IRP 입력 및 출력

```cpp
auto mapped = ntl::ipc::try_map_io_buffers(
    device_object, irp, connection.value());
if (!mapped)
  return mapped.status();

if (const auto *input = mapped->input())
  send_descriptor(input->descriptor());
if (const auto *output = mapped->output())
  send_descriptor(output->descriptor());
if (const auto *control = mapped->control_input())
  send_control_descriptor(control->descriptor());
```

NTL은 major function, 장치 플래그, 제어 코드 및 현재 스택 위치를 읽습니다. 자동
IRP 어댑터는 다음을 지원합니다.

| 작업 | 논리 버퍼 |
| --- | --- |
| `IRP_MJ_WRITE` | `input()` |
| `IRP_MJ_READ` | `output()` |
| 버퍼링된 IOCTL/FSCTL | 하나의 백업 매핑에 대한 논리적 `input()` 및 `output()` |
| `METHOD_IN_DIRECT` | 시스템 헤더는 `control_input()`, 직접 페이로드는 `input()` |
| `METHOD_OUT_DIRECT` | 시스템 헤더는 `control_input()`, 직접 페이로드는 `output()` |
| IOCTL/FSCTL도 아님 | 독립적인 `input()` 및 `output()` |

`IRP_MN_USER_FS_REQUEST`와 `IRP_MN_KERNEL_CALL`만 FSCTL 버퍼 레이아웃을
사용합니다. 마운트, 검증 및 파일 시스템 로드 요청에는
`STATUS_NOT_SUPPORTED`를 반환합니다.

IRP를 완료 전과 완료 후 중 어느 시점에 관찰하는지는 변경 가능한 옵션 비트가
아니라 진입점 자체로 구분합니다.

```cpp
auto submitted = ntl::ipc::try_map_io_buffers(
    device_object, irp, connection);

auto completed = ntl::ipc::try_map_completed_io_buffers(
    device_object, irp, connection);
```

완료된 읽기 또는 출력 쿼리에는 두 번째 형식을 사용하십시오. 출력 길이는
`IoStatus.Information`으로 제한됩니다. `METHOD_IN_DIRECT` 버퍼는 입력으로
유지되므로 이 제한을 적용하지 않습니다. 경고 상태로 완료된 요청에도 유효한
출력이 있을 수 있으므로 이를 치명적 실패로 취급하지 않습니다.
`map_io_options`는 접근 및 격리 정책만 제어합니다. 따라서 호출자가 불리언
값 하나를 설정해 완료 전 호출을 실수로 완료 후 호출로 바꿀 수 없습니다.

### 페이지 안전 기본값 및 제로 복사

사용자 모드 MDL 매핑은 페이지 단위 VAD를 만듭니다. 논리적 하위 범위를
지정하더라도 서비스가 첫 페이지와 마지막 페이지의 나머지 부분에 접근하는 것을
막을 수 없습니다. 따라서 고수준 어댑터는 기본적으로 다음 정책을 사용합니다.

```cpp
ntl::ipc::map_io_options options{
    .user_mdls = ntl::ipc::user_mdl_policy::isolate_partial_pages,
};
```

- 전체 페이지를 정확히 포함하는 사용자 MDL은 제로 카피로 매핑됩니다.
- 부분 페이지 MDL, 전체 MDL보다 짧은 완료 하위 범위, 커널 요청자 버퍼,
  비페이징 풀 MDL에는 전체를 0으로 초기화한 격리 스테이징을 사용합니다.
- buffered 커널 저장소는 호출자가 명시적인 거부 정책을 선택하지 않는 한 항상
  스테이징을 사용합니다.
- buffered 제어 요청의 스테이징은 완료 전에 초기화된 입력 접두사만 복사합니다.
  더 큰 출력 용량은 0으로 유지하며, 되쓰기는 쓰기 가능한 논리적 입력/출력
  범위로 제한합니다.

서비스가 인접 애플리케이션 바이트를 보아도 된다고 신뢰하는 드라이버는 다음과 같이
기존 제로 카피 동작을 선택할 수 있습니다.

```cpp
options.user_mdls =
    ntl::ipc::user_mdl_policy::allow_page_padding_exposure;
```

이 선택은 성능 힌트가 아니라 보안 결정입니다. 저수준
`try_map_mdl(borrowed_mdl_view, ...)` API에도 호출자가 같은 책임을 집니다. 이 API는
NTL이 MDL을 소유하지 않는다는 사실만 기록하며, 해당 MDL을 노출해도 안전하다고
보증하지는 않습니다.

`max_staging_bytes`, 연결의 매핑 개수 및 매핑 바이트 할당량은 클라이언트가
보유하는 메모리를 제한합니다. 매핑 바이트 할당량에는 논리 설명자의 길이만이 아니라
MDL 바이트 오프셋을 포함해 페이지 단위로 반올림한 전체 VAD 범위가 반영됩니다.
스테이징 및 교체 저장소 제한에도 페이지 단위로 반올림한 물리 할당량을 반영합니다.
복사 방식의 대체 경로를 허용할 수 없다면 `kernel_buffer_policy::reject`를
사용하십시오.

## 미니필터 매핑

`PFLT_CALLBACK_DATA`만으로는 출력이 유효한 시점인지 알 수 없으므로 콜백 단계도
형식의 일부입니다.

```cpp
auto write_input = ntl::flt::try_map_io_buffers(
    ntl::flt::as_pre(write_data), connection);

auto read_output = ntl::flt::try_map_io_buffers(
    ntl::flt::as_post(read_data), connection);
```

어댑터는 create EA, read/write, query/set 파일 정보, query/set EA, query/set 볼륨
정보, query/set 보안, query/set quota, 디렉터리 query/notification(확장 알림 포함),
FSCTL, IOCTL을 지원합니다. post 출력 길이는 `STATUS_BUFFER_OVERFLOW` 같은 경고
상태에서도 `IoStatus.Information`으로 제한합니다. NTL은 pre 버퍼를 매핑하면서
`FltLockUserBuffer`를 호출할 수 있지만 post 매핑에서는 새 MDL을 만들지 않습니다.
기존 MDL이 없는 post 사용자 포인터는 참조된 Filter Manager 요청자 프로세스에
연결한 상태에서 복사합니다.

매핑 도우미에는 `PASSIVE_LEVEL`이 필요합니다. pre/post 콜백이 이미 안전한
실행 수준이 아니라면 먼저 큐에 넣으십시오.

```cpp
auto status = ntl::flt::queue_post_operation_at_passive(
    data.native(), &finish_read_at_passive, completion_context);
if (status.is_ok())
  return ntl::flt::post_result::more_processing_required;
```

지연 루틴은 작업을 직접 완료하거나
`pending_pre_operation_queue`/`pending_post_operation_registry`에 성공적으로 넘긴
뒤에만 `STATUS_PENDING`을 반환합니다. post 지연이 실패하면 변환을 실패 처리하고
완료 컨텍스트를 인수해 정리 전용 `complete()`를 실행한 뒤 처리가 끝났다고
반환합니다. 전달 후 이 정리는 DISPATCH_LEVEL에서도 안전합니다. 이 대체 경로에서는
사용자 매핑이나 원시 버퍼 되쓰기를 시도하지 않습니다.

## 출력 후 액세스 준비

미니필터는 버퍼를 교체하지 않고도 안전한 post 콜백에서 원래 출력을 검사할 수
있습니다. pre 단계에서 준비 작업을 수행하며 `FltDecodeParameters`로 작업 출력을
찾고, 해당 작업이 MDL 필드를 제공할 때만 사용자 주소 버퍼를 잠급니다. Filter
Manager는 `FltLockUserBuffer`가 만든 모든 MDL을 소유합니다.

```cpp
struct fsctl_state {
  ntl::flt::prepared_fsctl_output_buffer prepared_output;

  explicit fsctl_state(
      ntl::flt::prepared_fsctl_output_buffer&& output) noexcept
      : prepared_output(std::move(output)) {}
};

auto prepared =
    ntl::flt::try_prepare_output_buffer(ntl::flt::as_pre(data));
if (!prepared ||
    completion.try_emplace(std::move(*prepared)).is_err())
  return ntl::flt::pre_result::success_no_callback;
return ntl::flt::pre_result::success_with_callback;
```

쌍을 이루는 안전 콜백은 범위가 지정된 변경 가능한 보기를 얻습니다.

```cpp
void finish_fsctl(
    ntl::flt::safe_file_system_control_operation operation,
    ntl::flt::related_objects,
    ntl::flt::completion_ref<fsctl_state> completion) noexcept {
  auto data = operation.data();
  const ntl::status accessed = completion->prepared_output.try_visit(
      operation,
      [&](ntl::flt::prepared_output_buffer_view view) noexcept {
        rewrite_records(view.data(), view.valid_size(), view.capacity());
      });
  if (accessed.is_err())
    data.set_io_status(accessed, 0);
}
```

`try_visit()`는 `FltLockUserBuffer`를 호출하거나 MDL을 해제하지 않습니다.
`valid_size()`는 `IoStatus.Information`으로 제한되며 visitor 전체를 NTL의 SEH
가드 안에서 실행합니다. 포인터를 visitor 밖으로 가져가면 안 됩니다. 하위 필터가
pre-operation에서 검증한 MDL이나 시스템 버퍼를 더는 제공하지 않는다면, 새
post-operation 소유권을 만들지 않고 접근을 실패 처리합니다.

## 작업 독립적인 미니필터 교체

`swapped_io_buffers`는 `input()`, `output()`, `control_input()`을 노출하면서 내부에서
올바른 IOPB 필드를 선택합니다. read/write에만 국한되지 않으며 위에 열거한
미니필터 작업과 buffered/direct/neither FSCTL/IOCTL 레이아웃을 지원합니다.
METHOD_NEITHER 제어 코드는 method 비트에 나타나지 않는 포인터 계약을 가질 수
있으므로 기본적으로 교체를 거부합니다. 대상 스택의 정확한 제어 코드를 검증한
경우에만 `allow_unverified_method_neither_control`을 설정합니다. 원래 출력만
변경하려면 대신 `prepared_output_buffer`를 사용합니다. 지원되지 않는 minor
function과 Fast I/O는 `STATUS_NOT_SUPPORTED`를 반환합니다. pre 콜백은 Fast I/O를
허용하지 않고 IRP 경로에서 재시도하게 할 수 있습니다.

작업 유형은 버퍼 선택기가 적합한지 여부도 제어합니다.

| 타입이 지정된 사전 작업 | API 양식 |
| --- | --- |
| EA 생성, 쓰기, SET_INFORMATION, SET_EA, SET_VOLUME_INFORMATION, SET_SECURITY, SET_QUOTA | `try_swap_io_buffers(operation, options)`; 입력이 추론됨 |
| 읽기, QUERY_INFORMATION, QUERY_VOLUME_INFORMATION, DIRECTORY_Control, QUERY_SECURITY | `try_swap_io_buffers(operation, options)`; 출력이 추론됨 |
| QUERY_EA, QUERY_QUOTA, FILE_SYSTEM_Control, DEVICE_Control, INTERNAL_DEVICE_Control | `try_swap_io_buffers(operation, swap_buffer::input/output/all, options)` |

`swap_io_options`에는 할당 및 실행 정책만 들어 있으며 입력이나 출력을 선택할 수
없습니다. READ/WRITE에 선택기를 지정하거나 양방향 작업에서 선택기를 생략하면
컴파일 시간에 거부됩니다.

```cpp
auto read = ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(read_data));

auto query_ea = ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(query_ea_data),
    ntl::flt::swap_buffer::output);
```

제어 작업에서 `swap_buffer::input`은 `control_input()`으로 노출되는 제어 헤더를
포함합니다. 실제 전송 방식은 계속 IOPB 제어 코드에서 결정하며 호출자가 buffered,
direct, neither I/O를 임의로 선택하지 않습니다. `METHOD_BUFFERED`는 양방향에 같은
backing 버퍼를 사용하므로, 입력 변환만 선택했어도 하위 드라이버의 출력을 되쓸 수
있도록 NTL이 내부 출력 슬롯을 유지하고 `output()`으로 노출합니다. 런타임
레이아웃에 선택한 방향이 없는 전송 방식은 `STATUS_NOT_SUPPORTED`를 반환합니다.

각 교체는 격리된 페이지를 사용합니다. 작업에 MDL 필드가 있으면 NTL은 잠긴
backing MDL에서 별도의 부분 교체 MDL을 만듭니다. 페이지 버퍼의 원본 MDL은 NTL이
계속 소유하고, Filter Manager는 설치된 부분 MDL만 소유해 post-operation 뒤
해제합니다. Filter Manager가 post 콜백에 원래 IOPB 매개변수를 제공하므로 완료
컨텍스트 정리는 해당 필드를 다시 쓰거나 두 번째로 dirty 표시하지 않습니다.
비캐시 read/write 교체 용량은 볼륨 섹터 크기로 반올림하지만 공개 뷰와 되쓰기
범위는 원래 논리 길이로 제한합니다.

사용자 매핑은 `apply()`, 출력 되쓰기 또는 완료 전에 닫아야 합니다.
`release_completion_context()`는 `apply()` 후에 지원되는 소유권 이전 경로입니다.
이 함수는 소유자를 post 콜백으로 옮기는 동시에 교체 MDL을 Filter Manager에
넘겼다고 표시합니다.

### 사전 쓰기 암호화

교체를 수행하면 애플리케이션의 원래 쓰기 버퍼가 변경되지 않은 상태로 유지됩니다.

```cpp
auto swapped =
    ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(write_data));
if (!swapped)
  return ntl::flt::pre_result::complete;

auto mapped = swapped->input()->try_map(
    crypto_connection, ntl::ipc::map_access::read_write);
if (!mapped)
  return ntl::flt::pre_result::complete;

// Send mapped->descriptor() to the service and wait/pend for encryption.
auto service_status = encrypt_with_service(mapped->descriptor());
auto close_status = mapped->close();
if (!service_status || close_status.is_err()) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}

auto apply_status = swapped->apply();
if (apply_status.is_err()) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}

auto context = swapped->release_completion_context();
if (!context) {
  (void)swapped->complete();
  return ntl::flt::pre_result::complete;
}
completion_context = *context;
return ntl::flt::pre_result::success_with_callback;
```

write post 콜백은 비페이지 교체 상태만 해제합니다.
`release_completion_context()`가 닫힌 매핑 레코드를 제거하고 비페이지 소유자와
제어 블록을 사용하므로, 이 정리 전용 경로는 `DISPATCH_LEVEL`까지 유효합니다.

```cpp
auto owner = ntl::flt::swapped_io_buffers::adopt_completion_context(context);
if (!owner)
  return STATUS_INVALID_PARAMETER;
return owner->complete();
```

### 읽은 후 암호 해독

pre-read는 출력 교체 저장소를 생성하고 설치한 뒤 소유권을 이전합니다. 하위 파일
시스템은 여기에 암호문을 씁니다. 패시브 post-read 작업자는 다음과 같이 처리합니다.

```cpp
auto owner = ntl::flt::swapped_io_buffers::adopt_completion_context(context);
if (!owner)
  return STATUS_INVALID_PARAMETER;

auto mapped = owner->output()->try_map(
    crypto_connection, ntl::ipc::map_access::read_write);
if (!mapped) {
  (void)owner->complete();
  return mapped.status();
}

auto service_status = decrypt_with_service(
    mapped->descriptor(), data->IoStatus.Information);
auto close_status = mapped->close();
if (!service_status || close_status.is_err()) {
  (void)owner->complete();
  return STATUS_DATA_ERROR;
}

auto copy_status = owner->copy_back(
    ntl::flt::as_post(ntl::flt::read_callback_data{data}));
auto complete_status = owner->complete();
return copy_status.is_err() ? copy_status : complete_status;
```

`copy_back()`은 타입이 지정된 post 래퍼가 동일한 콜백 데이터를 참조하는지 확인하고,
오류 완료를 거부하며, 최대 `IoStatus.Information` 바이트만 복사합니다. 교체 버퍼
매핑, 서비스 호출, 되쓰기는 계속 `PASSIVE_LEVEL` 작업이므로 read 변환은
지연/보류 경로를 사용해야 합니다.

빌드 가능한
[`examples/minifilter/swap-buffers`](../../examples/minifilter/swap-buffers)
예제에는 더 작은 로컬 변환 방식이 들어 있습니다. `.ntlxor` 파일만 pre-WRITE와
post-READ에서 XOR 교체를 적용하고 일반 `.tmp` 스트림은 변경하지 않습니다.
추가 사용자 서비스 보류 상태 없이 타입이 지정된 단계, Fast I/O 대체 경로,
PASSIVE_LEVEL 지연, `IoStatus.Information` 길이 제한, 되쓰기, 종료 정리, 확장자
선택을 보여줍니다. XOR은 교체된 바이트를 눈에 보이게 할 목적으로만 사용하며,
예제 문서에서 제품용 암호화 설계가 아닌 이유를 설명합니다.

## 왕복 및 분해 보류 중

`pending_pre_operation_queue`는 인스턴스별로 초기화됩니다. IRP 취소에는
`FltCbdq`를 사용하고, 필터 관리자의 DISPATCH 수준 큐 콜백에는 스핀 잠금을
사용하며, 모든 매핑·교체 정리는 시스템 스레드에서 `PASSIVE_LEVEL`로
수행합니다. 큐 자체는 비페이지 인스턴스 또는 드라이버 상태에 저장하십시오.
취소 루틴에서 참조할 수 있는 요청 레코드와 제어 블록도 비페이지 풀에서
할당됩니다.

```cpp
auto request_id = pending_pre.pend(
    ntl::flt::as_pre(data), std::nullopt, std::move(swapped));
if (!request_id)
  return ntl::flt::pre_result::complete;

send_request(*request_id);
return ntl::flt::pre_result::pending;

// Service reply / timeout:
pending_pre.resume(id);
pending_pre.cancel(id, STATUS_IO_TIMEOUT);
```

`pending_post_operation_registry`는 서비스가 출력을 변경하는 동안 post 콜백 데이터를
소유합니다. 성공 응답은 매핑을 닫고 유효한 출력을 되쓴 뒤 IOPB 포인터를 복원하고
`FltCompletePendedPostOperation`을 호출합니다.

두 레지스트리는 단조 증가하는 세대를 포함한 고정 너비 요청 ID를 사용하며 오래된
응답을 거부합니다. 시간 초과, 취소, 연결 해제, 서비스 종료, 인스턴스 해체,
언로드는 모두 `cancel`, `shutdown`, 연결 닫기 경로로 합류해야 합니다. 페이징 I/O
왕복은 기본적으로 거부합니다. 커널 내부 작업에 교체를 선택할 수는 있지만 페이징
I/O의 사용자 매핑은 계속 비활성화합니다.

미니필터 인스턴스와 연관된 분리 작업은
`queue_instance_work_item(instance, callable)`을 사용해야 합니다. 이 함수는 원시
executive 작업 항목 대신 Filter Manager 일반 작업 항목을 할당해 큐에 넣으므로,
호출 가능 래퍼가 반환할 때까지 필터 rundown 참조가 유지됩니다. 작업자는
`context_ref::reference()`로 얻은 별도의 타입이 지정된 컨텍스트 소유자도 유지해야
합니다.

```cpp
auto worker_context = instance_context.reference();
auto queued = ntl::flt::queue_instance_work_item(
    instance_context->instance,
    [owner = std::move(worker_context), request_id] {
      owner->pending_pre.resume(request_id);
    });
```

큐 삽입이 실패해도 호출자는 원래 컨텍스트 참조를 계속 소유하며,
`STATUS_PENDING`을 반환하기 전에 레지스트리 항목을 취소해야 합니다.

매핑 해제가 오류를 보고하고 `has_open_mappings()` 또는
`has_open_user_mappings()`가 여전히 true라면 보류 레지스트리가 I/O를 완료해서는
안 됩니다. 사용자 VAD가 살아 있는 동안 빌린 MDL이나 원래 버퍼 수명이 끝날 수
있기 때문입니다. 요청 ID는 재시도할 수 있습니다. 연결의 프로세스 레지스트리를
닫은 뒤 같은 ID로 `resume()`/`cancel()` 또는 `reply()`/`cancel()`을 다시
호출합니다. 종료 처리도 활성 매핑이 있는 요청의 강제 완료를 거부합니다.

## 사용자 모드 유효성 검사

`mapped_address`를 직접 포인터로 변환하지 마십시오. 먼저 설명자와 세션 세대를
검증합니다.

```cpp
ntl::ipc::mapped_client_buffer buffer;
auto result = ntl::ipc::mapped_client_buffer::open(
    descriptor, negotiated_generation, buffer);
if (result != ntl::ipc::validation_status::success)
  return protocol_error();
```

래퍼는 매핑 ID/세대, 예약 필드, 접근 권한, 포인터 너비 및 범위 오버플로를
검증합니다. 읽기 전용 매핑에서는 `writable_data()`가 null입니다.

## 수명 및 IRQL 계약

필수 순서는 다음과 같습니다.

1. 사용자 접속을 중지하고 서비스 요청을 수신/취소합니다.
2. 대상 프로세스 매핑을 닫습니다.
3. 스테이징 또는 교체된 출력을 되씁니다.
4. 원래 IOPB 필드를 복원합니다.
5. 소유한 MDL 및 격리된 페이지를 해제합니다.
6. I/O를 재개하거나 완료합니다.

매핑, 대상 프로세스 매핑 해제, 페이지 할당, 되쓰기, 서비스 대기는
`PASSIVE_LEVEL` 작업입니다. 활성 매핑이 없는 전달된 교체 컨텍스트는
`DISPATCH_LEVEL`까지 정리 전용 `complete()`를 수행할 수 있습니다. 해당 backing과
제어 블록은 명시적으로 비페이징입니다. 빌린 MDL 매핑이 IRP나 Filter Manager
작업보다 오래 지속되게 해서는 안 됩니다.

## 런타임 및 Driver Verifier 검증

일반 WDM 드라이버/앱 테스트는 실제 페이지 격리 매핑을 호출 프로세스에 노출합니다.
사용자 읽기/쓰기, 커널 관찰, 명시적 닫기, 핸들 닫기 시 강제 정리, 프로세스 종료
처리, 세대 변경, 주소 무효화, 자동 BUFFERED/IN_DIRECT/OUT_DIRECT/NEITHER 의미와
스테이징 되쓰기를 검증합니다. buffered 사례는 입력보다 큰 출력 용량의 스테이징
꼬리가 0으로 채워졌는지도 확인합니다. `scripts/ci/Run-DriverTests.ps1`로 실행하며,
`-RequireVerifier`를 지정하면 드라이버가 활성 Driver Verifier 대상이 아닌 경우
실행을 거부합니다.

`test/flt/runtime/io_buffer_*`의 전용 미니필터 쌍은 실제 연결된 사용자 서비스를
통해 pre-WRITE 교체와 post-READ 되쓰기를 검증합니다. 포트 연결 콜백이 서비스
프로세스를 캡처하고 각 교체 버퍼를 해당 프로세스에 매핑한 뒤, 타입이 지정된
`mapped_buffer_descriptor`를 사용자 요청 처리기에 보냅니다. 처리기는 논리적으로
유효한 입력 또는 완료 출력 범위만 변경합니다. 드라이버는 입력 적용이나 출력
되쓰기 전에 매핑을 동기적으로 닫고, 앱은 I/O 반환 후 VAD가 무효화됐는지
확인합니다. 또한 0바이트 EOF 완료, 서비스 연결 해제 시 거부, 언로드/재로드 수명,
필터 아래에 저장된 암호문을 검증합니다. 결정적 게이트는 활성 매핑이 있는 보류
pre-WRITE와 post-READ를 붙잡고, 앱은 양방향의 서비스 시간 초과, 활성 포트 연결
해제, 필터 해체를 실행합니다. 각 사례는 I/O 실패와 VAD 무효화를 요구하며, 시간
초과와 연결 해제 후 보류 레지스트리 카운터와 작업자 상태가 남지 않는지도
검증합니다.

대상 IRP의 pre 콜백과 성공한 read의 post 콜백은 처음부터 PASSIVE_LEVEL이어도
의도적으로 보류하므로, 성공한 실행은 두 지연 완료 도우미를 반드시 거칩니다.
Fast I/O는 허용하지 않고 시스템이 IRP 경로로 재시도할 때 검사합니다. 관리자 권한
실행기는 `scripts/ci/Run-FltIoBufferRuntimeTests.ps1`입니다. 이 테스트에는 테스트
서명이 활성화된 폐기 가능한 VM이 필요하며, 테스트 코드를 컴파일한 것만으로는
커널 측 검증 항목을 실행했다고 볼 수 없습니다.
