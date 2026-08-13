# 미니필터 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::flt`는 Windows 파일 시스템 미니필터를 위한 NTL 진입점 및 콜백
계층입니다. 필터 등록, 인스턴스 순서, I/O 콜백 디스패치 및 해제는 계속
Filter Manager가 담당합니다. NTL은 타입이 지정된 비소유 뷰를 제공하고,
등록된 필터가 사용하는 콜백 테이블의 수명을 유지합니다.

읽기 쉬운 드라이버 및 앱 예제는
[미니필터 샘플 목록](../../examples/minifilter)을 참고하십시오. WDK 샘플과
검증된 NTL 기능의 대응 관계는
[WDK 미니필터 샘플 지원 범위 표](../../test/flt/WDK-SAMPLE-COVERAGE.ko-KR.md)에
정리되어 있습니다.

## 진입점과 등록

`ntl::flt::main`을 정의하고 등록 객체를 드라이버로 이동합니다.

```cpp
#include <ntl/flt/all>

ntl::status ntl::flt::main(ntl::flt::driver& driver,
                           std::wstring_view) {
  ntl::flt::registration callbacks;
  callbacks.on(
      ntl::flt::operation::create,
      [](ntl::flt::create_callback_data, ntl::flt::related_objects,
         void*& completion) noexcept {
        completion = nullptr;
        return ntl::flt::pre_result::success_no_callback;
      });
  return driver.start(std::move(callbacks));
}
```

`ntl::flt::operation`은 원시 `IRP_MJ_*` 값을 노출하지 않고 Filter Manager
작업을 나타냅니다. pre-operation 콜백만 등록할 때는 플래그를 직접 전달할 수
있습니다.

```cpp
callbacks.on(ntl::flt::operation::write, pre_write,
             ntl::flt::operation_flags::skip_paging_io);
```

post-operation 콜백 자리를 채우기 위한 `nullptr`는 필요하지 않습니다.
`operation_flags`를 받는 오버로드는 `operation::read`와
`operation::write`에만 있습니다. 읽기/쓰기 전용 플래그를 다른 작업에
전달하면 런타임 등록 실패가 아니라 컴파일 오류가 발생합니다.

CMake에서는 미니필터 모델을 명시적으로 선택합니다.

```cmake
crtsys_add_driver(my_filter MINIFILTER NTL driver/main.cpp)
```

NuGet 패키지를 사용하는 Visual Studio WDK 프로젝트에서는 **프로젝트 속성 >
드라이버 설정 > 드라이버 모델**로 이동한 다음 **crtsys WDM 진입점**을
**NTL Minifilter**로 설정하는 것이 가장 간단합니다. 패키지의 MSBuild 대상이
다음 두 속성을 기록합니다.

```xml
<DriverType>WDM</DriverType>
<CrtSysIsMinifilter>true</CrtSysIsMinifilter>
<CrtSysUseNtlFltMain>true</CrtSysUseNtlFltMain>
```

이 설정은 `CrtSysNtlFltDriverEntry`를 선택하고 crtsys 런타임을 초기화하며,
`fltmgr.lib`를 링크하고 `ntl::flt::main`을 호출합니다. 또한 런타임을 해제하기
전에 필터 등록을 해제합니다.

### 미리 빌드된 ABI와 Windows 대상 버전

`crtsys.lib`는 Windows 8 Filter Manager 선언으로 빌드되지만 `ntl::flt`는
Windows 7 사용 코드도 지원합니다. 따라서 공개 형식인 `ntl::flt::driver`와
`ntl::flt::registration`의 레이아웃에는 WDK 버전에 따라 달라지는 저장
공간이 직접 들어가지 않습니다.

- Windows 8 전용 section 콜백 슬롯은 모든 Windows 7 이상 빌드에서 같은
  크기의 타입 소거 저장 공간을 예약합니다. 단, 등록 API는 `FLT_MGR_WIN8`을
  사용할 수 있을 때만 노출됩니다.
- `FLT_REGISTRATION`은 라이브러리를 사용하는 번역 단위에서 간접 할당하고,
  그 번역 단위의 `sizeof(FLT_REGISTRATION)` 및
  `FLT_REGISTRATION_VERSION` 값으로 채운 뒤 사용 측에서 제공한 삭제자를
  통해 해제합니다.

따라서 Windows 7 미니필터 프로젝트는 C++ 멤버 오프셋을 바꾸지 않고도 해당
NuGet 사전 빌드 라이브러리를 링크할 수 있으며, Windows 8 이상 프로젝트에는
section 알림 필드가 그대로 제공됩니다. 컴파일/링크 테스트 행렬은 양쪽을 각각
`0x0601`과 `0x0602`로 빌드하고 두 공개 객체의 크기를 하나의 필수 템플릿
기호에 인코딩합니다. 이후 조건부 레이아웃 회귀가 생기면 언로드 중 객체를
손상시키는 대신 빌드 단계에서 실패합니다.

## 레거시 제어 장치

미니필터는 Filter Manager 통신 포트와 별도로 일반 WDM 제어 장치를 노출할 수
있습니다. `driver::add_control_device()`는 기존의 타입이 지정된 `ntl::device` 및
`ntl::ioctl` 기능을 미니필터의 진입, 시작, 실패 및 언로드 수명 주기에
연결합니다.

```cpp
#include <ntl/flt/all>

struct cdo_state {
  std::atomic<bool> open{false};
};

struct ping_contract {
  using input_type = ping_request;
  using output_type = ping_reply;
  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x900;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr ULONG code =
      CTL_CODE(device_type, function, method, access);
};

using ping_ioctl = ntl::ioctl_from_contract<ping_contract>;

auto options = ntl::device_options()
                   .name(L"ProductControl")
                   .type(FILE_DEVICE_UNKNOWN);

auto status = driver.add_control_device<cdo_state>(
    std::move(options),
    [](ntl::device<cdo_state>& device) -> ntl::status {
      auto* state = &device.extension();
      device
          .on_create([state](ntl::irp& request) {
            state->open.store(true);
            request.succeed(FILE_OPENED);
          })
          .on_cleanup([state](ntl::irp& request) {
            state->open.store(false);
            request.succeed();
          })
          .on_close([](ntl::irp& request) { request.succeed(); })
          .on_device_control(
              [](const ntl::device_control::code& code,
                 const ntl::device_control::in_buffer& input,
                 ntl::device_control::out_buffer& output) {
                if (!ntl::is_ioctl<ping_ioctl>(code))
                  throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                       "unknown IOCTL");
                const auto* request =
                    ntl::ioctl_input_as<ping_ioctl>(input);
                if (!request)
                  throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                       "short input");
                ping_reply reply{/* ... */};
                if (!ntl::ioctl_write_output<ping_ioctl>(output, reply))
                  throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                       "short output");
              });
      return STATUS_SUCCESS;
    });
```

짧은 이름을 사용하면 `\\DosDevices\\ProductControl`이 게시되고 사용자 모드에서는
`\\.\ProductControl`로 엽니다. 선택적인 세 번째 인수로 완전한 DOS 링크 이름을
지정할 수 있습니다. 설치 프로그램이 명명된 레거시 장치의 ACL을 제공하지 않는다면,
제품이 소유한 setup class GUID와 함께 `device_options::security_descriptor()`를
사용하십시오.

등록 요청은 `start()` 전까지 대기합니다. NTL은 `FltRegisterFilter`를 호출하고
장치를 생성·구성한 뒤 link를 게시하고 `FltStartFiltering`을 호출합니다. 시작이
실패하거나 unload를 허용하면 장치를 삭제하기 전에 symbolic link를 제거합니다.
드라이버 source는 `DriverObject->MajorFunction`을 직접 설정하거나 `fltKernel.h`를 포함하지
않습니다. NTL 미니필터 진입점이 create, cleanup, close 및 device-control IRP를
타입이 지정된 핸들러로 전달합니다.

열린 CDO가 있으면 선택적 미니필터 언로드를 거부해야 할 수 있습니다. 이 정책은
장치 상태에서 추적하고, `registration::on_unload`에서
`!flags.mandatory()`인 동안 `STATUS_FLT_DO_NOT_DETACH`를 반환하십시오. 언로드
콜백이 요청을 수락한 뒤에는 `ntl::flt::driver`가 엔드포인트 해체를 담당하므로
장치를 별도로 삭제하면 안 됩니다.

[CDO 런타임 픽스처](../../test/flt/runtime/CDO-README.ko-KR.md)는
사용자 모드 열기, 단일 열기 정책, 타입이 지정된 IOCTL, 실제 선택적 언로드 거부,
거부 후에도 계속되는 디스패치, cleanup/close, 다시 열기 및 최종 언로드를
검증합니다.

## 인스턴스와 고도

등록된 필터와 볼륨에 연결된 인스턴스는 수명이 서로 다른 단위입니다. INF는 하나
이상의 명명된 인스턴스 구성을 정의하며, 각 구성에는 고도와 연결 플래그가 있습니다.
Filter Manager는 구성이 볼륨에 연결될 때마다 별도의 `PFLT_INSTANCE`를
생성합니다. 따라서 하나의 기본 정의가 여러 볼륨에서 여러 런타임 인스턴스를 만들 수
있고, 하나의 볼륨에도 서로 다른 고도에서 명시적으로 선택한 여러 정의가 연결될 수
있습니다.

고도는 설치 메타데이터이며 `registration::on()`이나 `driver::start()`의 인수가
아닙니다. 프로덕션 인스턴스는 구형 Windows의 `Instances` 레이아웃과 Windows 11
24H2의 `Parameters\Instances` 레이아웃 모두에 정의하십시오.

```ini
HKR,"Parameters\Instances","DefaultInstance",0x00000000,%DefaultInstance%
HKR,"Parameters\Instances\%DefaultInstance%","Altitude",0x00000000,%DefaultAltitude%
HKR,"Parameters\Instances\%DefaultInstance%","Flags",0x00010001,0
HKR,"Parameters\Instances\%SecondaryInstance%","Altitude",0x00000000,%SecondaryAltitude%
HKR,"Parameters\Instances\%SecondaryInstance%","Flags",0x00010001,1
```

`Flags=0`은 자동 연결을 허용합니다. `Flags=1`은 자동 연결을 막고 해당 이름의
정의를 명시적으로 선택할 수 있게 합니다. 실제 제품은 Microsoft의 미니필터 고도
정책에 따라 할당받은 고도를 사용해야 하며, 샘플의 값은 개발 용도로만 사용해야
합니다.

콜백 안에서 `objects.instance()`는 현재의 정확한 연결을 식별합니다.
`instance_context<T>`는 그에 따라 각 필터/볼륨/고도 연결마다 별도의 상태를
저장합니다. 진단이나 정책에 안정적인 식별자가 필요하면 `PASSIVE_LEVEL`에서
조회하십시오.

```cpp
auto information = objects.instance().try_information();
if (information) {
  // information->name, altitude, volume_name, and filter_name are owning
  // strings and remain valid after the Filter Manager query buffer is freed.
}
```

이미 `ntl::flt::volume`을 소유한 커널 코드는 필터 객체를 통해 연결을 명시적으로
관리할 수 있습니다.

```cpp
auto attached = driver.filter().try_attach(volume, L"Product Secondary");
if (!attached)
  return attached.status();

auto identity = attached->view().try_information();
// instance_ref releases the rundown reference; detaching is a separate action.
driver.filter().try_detach(volume, L"Product Secondary");
```

`try_attach()`는 설치된 INF에서 명명된 정의의 특성을 읽습니다. 진단 목적으로
위치를 직접 지정할 때는 `try_attach_at()`을 사용할 수 있지만, 프로덕션에서는
일반적으로 INF에 이름을 붙여 정의하는 방식이 계약입니다. `try_instances()`는
필터가 소유한 현재 연결을 모두 열거합니다. 이 도우미들은 소유권이 있는 STL 문자열과
벡터를 사용하므로, 하위 Filter Manager 쿼리가 `APC_LEVEL`을 허용하더라도 NTL
계약상 `PASSIVE_LEVEL`에서 호출해야 합니다.

네이티브 Filter Manager 등록의 `InstanceQueryTeardownCallback`이 null이면
수동으로 분리할 수 없습니다. NTL은 기본적으로 허용 콜백을 등록하므로 빈 사용자
콜백 없이도 `FilterDetach`, `FltDetachVolume` 및 `filter::try_detach()`가
동작합니다. 드라이버가 미완료된 인스턴스별 작업을 검사하고 필요하면
`STATUS_FLT_DO_NOT_DETACH`를 반환해야 할 때는
`on_instance_query_teardown()`을 사용하십시오. 항상 거부하려면
`deny_manual_detach()`를, NTL 기본 동작으로 복원하려면
`allow_manual_detach()`를 사용하십시오.

### 볼륨별 메타데이터 조정

`volume_metadata_file`은 `volume_metadata_instance_context<T>` 안에 두도록
설계되었습니다. 이 객체는 메타데이터 핸들과 참조된 파일 객체를 소유하고, 암시적
또는 명시적 볼륨 잠금·마운트 해제·query-remove 전에 이들을 닫습니다. 다시 열 때는
해당 작업을 일으킨 것과 같은 볼륨 `FILE_OBJECT`에 대해서만 엽니다.

```cpp
struct volume_state {
  ntl::flt::volume_metadata_file metadata;

  volume_state(ntl::flt::related_objects objects, std::wstring&& path)
      : metadata(objects, std::move(path)) {}
};

inline constexpr ntl::flt::volume_metadata_instance_context<volume_state>
    volume_context;

auto state = objects.try_get_or_create(
    volume_context, objects,
    std::wstring(L"\\System Volume Information\\ProductMetadata"));
if (!state)
  return state.status();

ntl::flt::volume_metadata_open_options options;
options.create_system_volume_information = true;
return (*state)->metadata.try_open(options);
```

이 특수 컨텍스트는 항상 `NonPagedPoolNx`를 사용하며, 의도적으로 풀을 선택하는
생성자를 제공하지 않습니다. `volume_metadata_file` 안에는 저장 공간이 항상
상주해야 하는 `ERESOURCE` 및 `KEVENT` 객체가 들어 있기 때문입니다. 일반적인
paged context나 다른 pageable 할당 영역에 배치하면 안 됩니다.

`ntl::file::is_volume_open()`은 볼륨 핸들을 식별합니다.
`create_parameters::is_implicit_volume_lock_candidate()`,
`file_system_control_parameters::volume_request()`, `pnp_parameters::request()`는
각각 해당하는 형식의 콜백을 분류합니다. 잠금, 마운트 해제 또는 query-remove 전에
`try_release_for(objects.file())`를 호출하십시오. 잠금이나 마운트 해제가 실패한
뒤, 잠금이 성공적으로 해제된 뒤, 암시적 잠금의 cleanup 때 또는 remove가 취소된
뒤에는 `try_reopen_for(objects.file())`를 호출하십시오.

명시적 잠금 해제에 성공하면 이전 볼륨 인스턴스가 무효화될 수 있습니다. 이 경우
`try_reopen_for()`가
`STATUS_INVALID_DEVICE_OBJECT_PARAMETER`, `STATUS_FILE_INVALID` 또는
`STATUS_NO_MEDIA_IN_DEVICE`를 반환하는 것은 정상입니다. 다시 마운트된 볼륨의
instance setup이 새 메타데이터 소유자를 여는 역할을 합니다. 따라서 이 상태 값들을
메타데이터가 영구적으로 사라졌다는 증거로 해석하면 안 됩니다.

스냅샷 조정에는 작업 전 콜백부터 다른 작업 후 스레드까지 `ERESOURCE`를 계속
보유하는 대신, 스레드 간에 안전하게 이동할 수 있는 이동 전용 토큰을 사용합니다.
`try_begin_update()`는 업데이트를 승인합니다.
`try_hold_updates_for_snapshot()`은 새 업데이트를 차단하고 이미 승인된 작업이
끝나기를 기다리며, 작업 후 콜백에서 토큰이 소멸하면 업데이트를 재개합니다.
`device_control_parameters::is_snapshot_flush_and_hold()`는 스냅샷 요청을
식별합니다.

[MetadataManager 런타임 픽스처](../../test/flt/runtime/METADATA-README.ko-KR.md)는
잠금 해제로 발생하는 재마운트와 성공적인 마운트 해제·분리·재마운트를 포함해 이
경로들을 ReFS에서 검증합니다.

## I/O 작업 외의 등록 콜백

`registration`은 현재 `FLT_REGISTRATION`이 제공하는 I/O 작업 외 콜백 슬롯을
타입이 지정된 콜백 시그니처로 노출합니다. 원시 `FLTAPI`, `PFLT_*` 및 `PVOID*`
매개변수는 NTL의 네이티브 트램펄린 내부에만 머뭅니다.

```cpp
callbacks
    .on_generate_file_name(generate_name)
    .on_normalize_name_component(normalize_component)
    .on_normalize_context_cleanup(cleanup_normalization);

#if FLT_MGR_LONGHORN
callbacks
    .on_transaction_notification(transaction_state_context,
                                 transaction_notification)
    .on_normalize_name_component_ex(normalize_component_ex);
#endif

#if FLT_MGR_WIN8
callbacks.on_section_notification(section_state_context, section_conflict);
#endif
```

transaction 및 section 오버로드는 전달된 컨텍스트 선언을 등록하고 그 C++ 상태
형식을 콜백에 바인딩합니다. 같은 선언을 `registration::context()`에도 전달하면
안 됩니다. 버전에 종속된 콜백은 선택한 WDK가 지원할 때만 노출됩니다.

`name_control`은 파일 이름 생성 콜백에 전달되는 출력 버퍼를 감쌉니다.
`try_assign()`과 `try_append()`는 `FltCheckAndGrowNameControl`을 통해 버퍼를
확장하며, 버퍼 자체의 소유권은 계속 Filter Manager에 있습니다.

### 이름 공급자 출력

`name_generation_request`에는 인스턴스, 파일, 선택적인 작업 독립
`callback_data_view` 및 파싱된 옵션 뷰가 들어 있습니다.
`name_generation_output`에는 비소유 `name_control`과 캐시 여부가 들어 있습니다.

```cpp
ntl::status generate_file_name(
    ntl::flt::name_generation_request request,
    ntl::flt::name_generation_output output) noexcept {
  if (!request.target_instance() || !request.target_file() || !output)
    return STATUS_INVALID_PARAMETER;

  auto status =
      output.name().try_assign(LR"(\Device\Volume\mapped.txt)");
  if (status.is_err())
    return status;

  output.set_cache(true);
  return STATUS_SUCCESS;
}
```

이름 공급자는 대개 자신보다 아래에 있는 공급자가 보고한 이름을 사용해야 합니다.
이 조회에는 `request.try_query_lower_name(options)`을 사용하십시오. 콜백 데이터가
있으면 `FltGetFileNameInformation`을, 없으면
`FltGetFileNameInformationUnsafe`를 선택합니다. 또한 공급자가 자신을 다시
호출하는 일을 막기 위해 항상 `FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER`를
제거합니다.

```cpp
auto lower = request.try_query_lower_name(
    FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
    FLT_FILE_NAME_DO_NOT_CACHE);
if (!lower)
  return lower.status();
if (auto status = lower->try_parse(); status.is_err())
  return status;
```

등록 콜백에는 `related_objects`가 전달되지 않습니다. 볼륨별 정책이
`instance_context<T>`에 있다면 타입이 지정된 인스턴스에서 직접 가져오십시오.

```cpp
auto mapping = request.target_instance().try_get(mapping_context);
if (!mapping)
  return mapping.status();
```

`name_normalization_request`도 인스턴스, 선택적인 파일, 상위 디렉터리, 볼륨
접두사, 구성 요소 및 정규화 플래그를 노출합니다.
`name_normalization_output`은 `FILE_NAMES_INFORMATION`에 쓰는 범위를 제한하고,
구성 요소 사이에서 공유할 `normalization_context` 슬롯을 제공합니다. 이 객체들은
모두 콜백이 실행되는 동안만 유효한 비소유 뷰입니다.

## 콜백 데이터 뷰

`ntl::flt::callback_data<Operation>`은 `FLT_CALLBACK_DATA`를 소유하지 않고
감싸는 뷰입니다. 모든 작업에는 `create_callback_data`, `write_callback_data`,
`cleanup_callback_data` 같은 공개 `<operation>_callback_data` 별칭이 있으므로
작업 자체가 콜백 서명의 일부가 됩니다. 이 뷰는 I/O 상태, 완료 처리, 작업 형식에
맞는 매개변수 및 RAII 파일 이름 조회 기능을 제공합니다.
`ntl::flt::related_objects`는 필터, 볼륨, 인스턴스 및 커널 `FILE_OBJECT`에 대한
타입이 지정된 뷰를 노출합니다.

저수준 네임스페이스 가상화에서는
`create_parameters::try_replace_target_name()`이 `IoReplaceFileObjectName`을 통해
대체 이름을 대상 `FILE_OBJECT`에 복사합니다. `clear_related_target()`은 완전한
절대 경로로 교체한 이름이 이전 related file object와 결합되는 일을 막습니다.
대부분의 simulated reparse 필터는 이 두 작업을 수행하고 필요한 완료 상태까지
원자적으로 설정하는 상위 수준 도우미를 사용하는 편이 좋습니다.

### Simulated reparse와 대상 경로 복구

`try_complete_reparse()`는 단계가 형식으로 구분된 pre-create 작업만 받습니다.
볼륨 또는 장치 접두사를 포함해 완전히 정규화한 대체 경로를 전달하고, 성공하면
`pre_result::complete`를 반환하십시오.

```cpp
auto status = ntl::flt::try_complete_reparse(
    ntl::flt::as_pre(data),
    LR"(\Device\HarddiskVolume3\physical-parent\physical-name)",
    ntl::flt::reparse_name_kind::absolute);
if (status.is_err()) {
  data.complete(status);
  return ntl::flt::pre_result::complete;
}
return ntl::flt::pre_result::complete;
```

이 도우미에는 `PASSIVE_LEVEL`에서 실행되는 IRP pre-create가 필요합니다. 파일 객체
이름을 바꾸고, 절대 경로로 바꿀 때는 `RelatedFileObject`를 지운 다음에야
`STATUS_REPARSE`와 `IO_REPARSE`로 요청을 완료합니다.

`IRP_MJ_NETWORK_QUERY_OPEN`은 보통 Fast I/O이므로 simulated reparse를 반환할
수 없습니다. 타입이 지정된 매개변수 뷰는 하위 create 옵션과 스택 플래그를
노출합니다. 조회한 이름이 매핑 대상이라면 `IoStatus`를 바꾸지 말고
`pre_result::disallow_fast_io`를 반환하십시오. 그러면 Filter Manager가 일반
reparse 도우미를 실행할 수 있는 느린 create 경로로 다시 요청합니다.

```cpp
ntl::flt::pre_result pre_network_query_open(
    ntl::flt::network_query_open_callback_data data,
    ntl::flt::related_objects, void*&) noexcept {
  auto parameters = data.parameters();
  if (data.is_fast_io_operation() && !parameters.paging_file() &&
      !parameters.open_by_file_id() && belongs_to_mapping(data)) {
    return ntl::flt::pre_result::disallow_fast_io;
  }
  return ntl::flt::pre_result::success_no_callback;
}
```

rename 및 hard-link 요청에는 서로 관련된 여러 네이티브 버퍼 레이아웃이 사용됩니다.
`set_information_parameters::destination()`은 길이를 검증한 뒤 `kind()`,
`information_class()`, `root_directory()`, `name()`, `flags()`, `extended()`,
`replace_if_exists()`를 제공하는 하나의 읽기 전용 뷰를 반환합니다. 네이티브 입력
버퍼를 노출하지 않으면서 rename/link, extended 및 bypass-access-check 정보
클래스를 모두 지원합니다.

```cpp
auto destination = data.parameters().destination();
if (!destination)
  return ntl::flt::pre_result::success_no_callback;

auto resolved = ntl::flt::try_query_destination_name(
    ntl::flt::as_pre(data),
    FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
        FLT_FILE_NAME_DO_NOT_CACHE);
if (!resolved)
  return ntl::flt::pre_result::success_no_callback;

auto replacement = rewrite_destination(*resolved);
(void)ntl::flt::try_reissue_destination(
    ntl::flt::as_pre(data), replacement);
return ntl::flt::pre_result::complete;
```

`try_reissue_destination()`은 정보 클래스와 rename/link 플래그를 보존하고,
크기가 제한된 임시 버퍼를 만든 뒤 현재 인스턴스 아래로
`FltSetInformationFile`을 호출합니다. 이어서 버퍼를 해제하고 반환된 상태로 원래
작업을 완료합니다. 이 함수는 `PASSIVE_LEVEL`에서 실행해야 하며 대체 경로는 같은
볼륨 안에 있어야 합니다.

이름 터널링에는 pre/post 단계에 걸친 수명 계약이 있습니다. pre-create 또는
pre-set-information에서 얻은 정규화된 `name_information`을 타입이 지정된 완료
상태에 보관하고, 짝이 되는 post 콜백에서
`try_get_tunneled_name(as_post(data), pre_name)`을 호출하십시오. 보관한 객체와
반환된 객체 모두 RAII `name_information` 소유자입니다. 파일 시스템에 터널링된
이름이 없으면 성공 결과에 빈 소유자가 들어갈 수 있습니다.

이 API들은 SimRep의 create, network-query-open, 대상 경로 및 터널링된 이름 처리
기능을 지원합니다. 하지만 디렉터리 열거, 알림, 쿼리 또는 파일 시스템 제어 결과를
자동으로 가상 네임스페이스에 맞춰 주지는 않습니다.
[SimRep 런타임 테스트](../../test/flt/runtime/SIMREP-README.ko-KR.md)는 완전히
격리된 드라이버/앱 쌍과 VM 검증 항목을 제공합니다. 여기에는 터널 이름 없이
성공하는 사례와 8.3 별칭을 보존된 긴 이름으로 복원하는 사례가 모두 포함됩니다.

[NameChanger 런타임 픽스처](../../test/flt/runtime/NAME-CHANGER-README.ko-KR.md)는
네임스페이스 이식에 필요한 추가 디렉터리 제어 작업을 보여줍니다.
콜백은 등록 경계에 타입이 지정된 상태로 유지되지만 검증자는
`NtQueryDirectoryFile`를 직접 사용하여 10개의 디렉터리 레이아웃을 요청하고
대상 파일 시스템/OS가 허용하는 모든 레이아웃을 실행합니다. 또한 패턴, 연속
상태, 작은 버퍼, 단일 항목 반환, 다시 시작 스캔 및 레코드 체인 무결성도
검증합니다. 이는 픽스처에 명시적으로 열거된 레이아웃의 테스트 범위일 뿐,
임의의 디렉터리 정보 클래스가 같은 바이너리 레이아웃을 공유한다는 보장은
아닙니다.

### Post-create에서 파일 열기 취소

성공한 post-create 콜백이 실행될 때는 하위 스택에서 감염됐거나 정책상 허용되지
않는 파일을 이미 열었을 수 있습니다. 단계가 형식으로 구분된 다음 도우미로 해당
열기를 취소하고 원래 create 결과를 바꾸십시오.

```cpp
void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects) noexcept {
  if (data.io_status().is_err() || data.io_status() == STATUS_REPARSE)
    return;

  if (scan_file(objects) == verdict::infected)
    (void)ntl::flt::try_cancel_file_open(ntl::flt::as_post(data));
}
```

`try_cancel_file_open()`은 `post_operation<operation_id::create>`만 받습니다.
`PASSIVE_LEVEL`, reparse가 아닌 성공한 create, `STATUS_ACCESS_DENIED` 같은 오류
상태, 아직 사용자 핸들이 생성되지 않은 파일 객체가 필요합니다. 이 함수는
`FltCancelFileOpen()`을 호출하고 원래 콜백 데이터를 information 값 0으로
완료합니다. pre-create 작업, 원시 콜백 데이터, 성공 완료 상태 또는
`FO_HANDLE_CREATED`가 설정된 파일은 거부합니다.

이 도우미는 현재 열기의 결과만 바꿀 뿐 이후의 열기에 적용할 영구 정책을 만들지는
않습니다. 동기화된 post-create 콜백이 아직 작업을 소유하고 있을 때 판정을
내리십시오. Microsoft의
[`FltCancelFileOpen`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcancelfileopen)
계약에도 같은 시점 제한이 명시되어 있습니다.

### 삭제 disposition과 cleanup 확인

삭제 요청이 있었다고 파일이 삭제됐다고 단정할 수는 없습니다. 요청이 실패하거나,
이후 요청으로 해제되거나, 다른 요청과 경합하거나, 마지막 핸들의 cleanup 때까지
보류될 수 있습니다. NTL은 네이티브 콜백 버퍼를 노출하지 않고 이 요청을 다룹니다.

```cpp
ntl::flt::pre_result pre_set_information(
    ntl::flt::set_information_callback_data data,
    ntl::flt::related_objects objects,
    ntl::flt::completion_slot<delete_state> &completion) noexcept {
  const auto disposition = data.parameters().disposition();
  if (!disposition)
    return ntl::flt::pre_result::success_no_callback;

  auto state = objects.try_get_or_create(my_stream_context);
  if (!state)
    return ntl::flt::pre_result::success_no_callback;

  if (completion.try_emplace(
          std::move(*state), disposition.state_kind(),
          disposition.delete_requested()).is_err()) {
    return ntl::flt::pre_result::success_no_callback;
  }
  return ntl::flt::pre_result::synchronize;
}
```

`disposition_information_view`는 `FileDispositionInformation`과
`FileDispositionInformationEx`를 모두 검증하고 작은 값만 복사합니다. 다음
접근자를 제공합니다: `delete_requested()`, `extended()`,
`state_kind()`, `flags()`, `posix_semantics()`,
`force_image_section_check()`, `on_close()`, `ignore_readonly_attribute()`.
의도적으로 네이티브 버퍼 접근자는 제공하지 않습니다.
`FILE_DISPOSITION_ON_CLOSE`가 설정된 확장 요청은 delete-on-close 상태를 제어하고,
그 밖의 레거시 또는 확장 요청은 일반 삭제 disposition을 제어합니다. create 콜백은
`data.parameters().delete_on_close()`로 초기 상태를 직접 확인할 수 있습니다.

set-information의 post 콜백이 성공한 뒤에만 추적 상태를 갱신하십시오. disposition
작업이 겹치면 완료 순서가 최종 상태를 정확히 나타내지 않을 수 있습니다. 마지막으로
관찰한 post 콜백을 확정 상태로 간주하지 말고 스트림 상태를 불확실하다고 표시한 뒤
cleanup 때 확인하십시오.

`try_query_cleanup_deletion()`은 단계가 형식으로 구분된 post-cleanup 작업만 받으며
`PASSIVE_LEVEL`에서 실행해야 합니다. 일반적으로 pre-cleanup 콜백에서 스트림
컨텍스트를 유지하고 `pre_result::synchronize`를 반환한 뒤, 짝이 되는 post
콜백에서 다음과 같이 조회합니다.

```cpp
void post_cleanup(
    ntl::flt::cleanup_callback_data data,
    ntl::flt::related_objects,
    ntl::flt::completion_ref<cleanup_state> completion) noexcept {
  auto deletion =
      ntl::flt::try_query_cleanup_deletion(ntl::flt::as_post(data));
  if (!deletion)
    return;

  if (*deletion == ntl::flt::cleanup_deletion_state::deleted)
    notify_once(*completion);
}
```

이 도우미는 `FltQueryInformationFile(FileStandardInformation)`에서 반환한
`STATUS_FILE_DELETED`를 `deleted`로, 성공한 조회를 `present`로 변환하고 다른
오류는 그대로 보존합니다.
[삭제 런타임 픽스처](../../test/flt/runtime/DELETE-README.ko-KR.md)는 삭제 취소,
create 시점의 delete-on-close, 확장 플래그, 보류 중인 핸들, 결정적으로 재현되는
disposition 경합, 대체 데이터 스트림과 전체 파일을 구분하는 검증을 포함한 완전한
드라이버/앱 예제를 제공합니다.

### 하위 스택의 작업 상태

`callback_data<Operation>::try_request_operation_status()`는
`FltRequestOperationStatusCallback`에 형식을 부여한 대응 API입니다. IRP 기반
요청의 pre-operation 콜백에서만 호출하십시오. 이 함수가 관찰하는 값은 하위 스택의
`IoCallDriver`가 반환할 때의 값이며, 작업의 최종 post-operation `IoStatus`를
대체하지 않습니다.

대부분의 필터에는 필요 없는 기능이므로 적용 범위가 의도적으로 좁습니다. 일반적인
용도는 oplock FSCTL과 디렉터리 변경 알림입니다. 여기서 `STATUS_PENDING`은 하위
스택이 비동기 요청을 받아들였다는 뜻입니다.

```cpp
struct notify_status_state {
  explicit notify_status_state(ULONG length) noexcept : length(length) {}
  ~notify_status_state() noexcept = default;

  ULONG length;
};

void observe_notify_status(
    ntl::flt::operation_status_snapshot<
        ntl::flt::operation_id::directory_control> snapshot,
    ntl::flt::related_objects objects,
    ntl::status operation_status,
    notify_status_state& state) noexcept {
  if (operation_status == STATUS_PENDING &&
      snapshot.parameters().is_notify() &&
      snapshot.parameters().length() == state.length) {
    // The lower file-system stack accepted this notification request.
  }
}

ntl::flt::pre_result pre_directory_control(
    ntl::flt::directory_control_callback_data data,
    ntl::flt::related_objects,
    void*&) noexcept {
  auto parameters = data.parameters();
  if (data.is_irp_operation() && parameters.is_notify()) {
    (void)data.try_request_operation_status(
        &observe_notify_status, parameters.length());
  }
  return ntl::flt::pre_result::success_no_callback;
}
```

콜백에는 원시 `PFLT_IO_PARAMETER_BLOCK` 대신 작업 형식에 맞는 읽기 전용 매개변수
스냅숏이 전달됩니다. Filter Manager는 요청 시점에 이 스냅숏을 캡처하므로 이후의
매개변수 변경은 반영되지 않습니다.

상태 객체는 nonpaged pool에 생성되며 NTL이 정확히 한 번 소멸시킵니다. 요청이
실패하면 즉시 소멸시키고, 성공하면 상태 콜백이 반환된 뒤 소멸시킵니다. 따라서 생성자와
소멸자는 `noexcept`여야 하고, 소유한 리소스는 `IRQL <= APC_LEVEL`에서 안전하게
해제할 수 있어야 합니다. 상태를 받는 오버로드는 풀 할당을 한 번 수행합니다. 요청별
상태가 필요 없다면 `try_request_operation_status<&callback>()`가 컴파일 타임
콜백을 사용하며 할당하지 않습니다. 작업 상태를 요청하지 않는 일반 콜백에도 할당
비용이 없습니다. 네이티브 제약은 그대로 적용되므로 pre-operation 처리 밖에서나
`IRP_MJ_CLOSE`에 대해서는 호출할 수 없습니다.

### 미니필터가 직접 발행하는 Filter Manager I/O

`try_allocate_callback_data()`는 미니필터가 직접 발행할 I/O의 콜백 데이터를
생성하고 이동 전용 `callback_data_owner`를 반환합니다. `prepare()`로 작업을
선택하고 해당 작업의 타입이 지정된 매개변수 뷰만 구성한 뒤 동기 또는 비동기 실행을
선택하십시오.

```cpp
auto request =
    ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
if (!request)
  return request.status();

FILE_STANDARD_INFORMATION information{};
auto query = request->prepare(ntl::flt::operation::query_information);
query.parameters().length(sizeof(information));
query.parameters().information_class(FileStandardInformation);
query.parameters().buffer(&information);
return request->perform_synchronously();
```

비동기 제출은 컴파일 타임 완료 콜백을 받습니다. 완료 콜백에 소유권이 직접
전달되며, `try_perform_asynchronously()`는 복사 가능한 작업 핸들을 반환합니다.

```cpp
void complete_io(ntl::flt::callback_data_owner data) noexcept {}

auto operation = request->try_perform_asynchronously<complete_io>();
if (!operation)
  return operation.status();

// May race safely with inline or concurrent completion.
(void)operation->cancel();
return operation->wait();
```

네이티브
[`FltPerformAsynchronousIo`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltperformasynchronousio)
계약에 따르면 제출이 오류를 반환해도 완료 콜백은 호출됩니다. 따라서 제출 직전에
원래 소유자는 비워집니다. 작업 핸들은 `FltCancelIo`와 완료 처리를 조정하여, 동시에
실행 중인 취소가 콜백 데이터를 사용하는 동안 데이터가 해제되지 않도록 합니다.
`wait()`는 명시적인 종료 대기 경계도 제공합니다.

오버로드 하나는 빌려 쓰는 형식 지정 컨텍스트를 받습니다.

```cpp
void complete_io(ntl::flt::callback_data_owner data,
                 io_state* state) noexcept;

io_state* nonpaged_state = acquire_long_lived_io_state();
auto operation =
    request->try_perform_asynchronously<complete_io>(nonpaged_state);
```

완료 콜백은 `IRQL <= DISPATCH_LEVEL`에서 실행될 수 있습니다. 컨텍스트와 콜백이
접근하는 메모리는 완료될 때까지 유효한 nonpaged 메모리여야 합니다.
`FltAllocateCallbackData`가 반환한 콜백 데이터에는 `FltSetCancelCompletion`을
호출하지 마십시오. 이 API는 아직 하위 IRP도 만들어지지 않은 생성 작업이 아니라,
기존에 들어온 IRP 기반 작업을 작업 큐에 올릴 때 사용하는 함수입니다.

### 제네릭 람다와 편집기 자동 완성

C++ 컴파일러는 제네릭 콜백을 허용하며, 작업 태그로부터 `auto` 매개변수의 형식을
구체화합니다.

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](auto data, auto objects, auto& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

`test/flt/compile/operation_callback.cpp`에서 이 형식을 계속 컴파일해 검사합니다.
그러나 애플리케이션 코드에 권장하는 표기는 아닙니다. 현재 Visual Studio
IntelliSense, Microsoft C/C++ VS Code 확장 및 clangd는 문맥상의 형식을
추론하거나 오류를 진단할 수 있지만, 제네릭 람다 안에서 `data.`를 입력했을 때 멤버
목록을 표시하지 못할 수 있습니다. 이는 편집기 또는 언어 서버의 자동 완성 한계이며
NTL 콜백 형식이나 C++ 컴파일의 문제가 아닙니다.

따라서 공개 샘플에서는 작업별 별칭을 명시하여 멤버 자동 완성, 코드 탐색 및 매개변수
검색을 사용할 수 있게 합니다.

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects,
       void*& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

네이티브 `FLT_PARAMETERS` 공용체의 멤버를 직접 고르지 말고 작업별 매개변수 뷰를
사용하십시오.

```cpp
callbacks.on(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data, ntl::flt::related_objects,
       void*&) noexcept {
      const auto write = data.parameters();

      const ULONG length = write.length();
      const LARGE_INTEGER offset = write.byte_offset();
      (void)length;
      (void)offset;
      return ntl::flt::pre_result::success_no_callback;
    });
```

`registration::on()`에 전달한 작업이 콜백 데이터의 C++ 형식을 결정합니다. create
등록은 `create_callback_data`를 받으며 이 형식의 `parameters()`는
`create_parameters`만 반환합니다. write 등록은 `write_callback_data`를 받고
`parameters()`는 `write_parameters`만 반환합니다. 작업과 콜백 데이터의 형식이
맞지 않으면 `on()` 오버로드를 결정하는 단계에서 거부되므로, IDE 의미 분석이 콜백
본문이나 런타임 등록까지 기다리지 않고 등록 호출 위치에서 오류를 보고할 수 있습니다.
코드 생성이나 일반화된 등록 로직에는 `callback_data<operation::close>` 같은 제네릭
표기도 계속 사용할 수 있습니다.

create 콜백 데이터에서 read 또는 write 매개변수를 요청하는 것은 불가능합니다.
매개변수 setter는 `FltSetCallbackDataDirty()`를 자동으로 호출합니다.
`native_iopb()`는 특수한 Filter Manager 작업을 위한 명시적인 탈출구로만
남아 있습니다.

`name_information`은 `FltGetFileNameInformation`이 반환한 참조를 소유합니다.
파싱된 경로 구성 요소를 읽기 전에 `try_parse()`를 호출하십시오. 소멸할 때는
`FltReleaseFileNameInformation`을 호출합니다. `driver.start()`가 성공한 뒤에는
`FltUnregisterFilter`를 직접 호출하지 마십시오. 필터 등록 해제와 crtsys 런타임
종료는 NTL 진입 계층이 담당합니다.

## 정상 완료 전용 post 콜백과 완료 상태

`void* CompletionContext`와 `post_operation_flags`를 받지 않는 post 콜백은 정상적인
I/O 완료 처리만 수행한다고 선언하는 형식입니다.

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects) noexcept {
      // Called only after normal completion, never while the instance drains.
      if (data.io_status().is_ok())
        record_success(objects);
    });
```

NTL은 이 콜백 뒤에 `FLT_POSTOP_FINISHED_PROCESSING`을 반환합니다. Filter Manager가
`FLTFL_POST_OPERATION_DRAINING`을 전달하면 이 콜백을 건너뜁니다. 이 형식은
pre-operation에서 post-operation이 반드시 해제해야 할 리소스를 획득하지 않는 경우에
적합합니다.

pre-operation에서 소유권이 있는 I/O별 상태를 post-operation으로 넘겨야 한다면
`on_with_completion<T>()`을 사용하십시오.

```cpp
struct request_state {
  std::uint32_t original_length;

  explicit request_state(std::uint32_t length) noexcept
      : original_length(length) {}
  ~request_state() noexcept = default;
};

callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_slot<request_state>& completion) noexcept {
      if (completion.try_emplace(data.parameters().length()).is_err())
        return ntl::flt::pre_result::success_no_callback;
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> completion) noexcept {
      if (completion && data.io_status().is_ok())
        record_bytes(completion->original_length);
    });
```

`completion_slot<T>`는 `NonPagedPoolNx`에서 `T`를 할당합니다. pre-operation이
`success_with_callback` 또는 `synchronize`를 반환할 때만 객체를 Filter Manager로
넘기며, 그 밖의 pre-operation 결과에서는 즉시 소멸시킵니다. `completion_ref<T>`는
현재 post 콜백 안에서만 유효한 비소유 뷰입니다.

NTL은 모든 경로에서 객체를 소유하고 소멸시킵니다.

| 경로 | 사용자 작업 후 콜백 | 소멸 시점 |
| --- | --- | --- |
| 작업 전 콜백이 작업 후 콜백을 요청하지 않음 | 호출 안 함 | 작업 전 콜백 반환 전 |
| 정상 완료 | 호출 | 작업 후 콜백 직후 |
| 정상 완료에서 WhenSafe 요청 | 즉시 콜백과 안전 콜백 모두 호출 | 안전 콜백 직후 |
| WhenSafe를 예약할 수 없음 | 즉시 콜백만 호출 | NTL이 완료 처리를 재개하기 전 |
| 인스턴스 종료 처리 중, 플래그 없는 타입이 지정된 작업 후 콜백 | 건너뜀 | 종료 처리 트램펄린에서 |
| 인스턴스 종료 처리 중, 플래그 인식 타입이 지정된 작업 후 콜백 | `flags.draining()`과 함께 호출 | 콜백 직후 |

완료 상태 소멸자는 풀 메모리, 컨텍스트 참조, 객체 참조, 런다운 보호 및 미완료
I/O 카운터처럼 해당 I/O만 소유하는 리소스를 해제하기에 적합합니다. 다만 일반
완료 로직을 대신하지는 않습니다. 소멸자에서 최종 I/O 데이터를 검사하거나 성공
통계를 갱신하거나 새 작업을 시작하면 안 됩니다.

`T`는 `try_emplace()`에 전달한 인수로 예외 없이 생성할 수 있고 예외 없이
소멸할 수 있어야 합니다. 소멸자와 모든 RAII 멤버는 종료 처리 중을 포함해 작업
후 콜백이 실행될 수 있는 IRQL에서 적법해야 합니다. 상태는 비페이지이며 블로킹하지
않아야 합니다. 드라이버가 종료 처리를 명시적으로 관찰하거나, 네이티브 작업 후
결과를 반환하거나, 완료를 보류하거나, 타입이 지정된 상태로 표현할 수 없는 소유권을
조정해야 한다면 플래그 인식 저수준 콜백을 사용하십시오.

## Post 및 WhenSafe 처리

post-operation 경로에 IRQL과 무관한 부분과 `IRQL <= APC_LEVEL`이 필요한 작업이
모두 있다면 같은 `on()` API를 사용합니다.

```cpp
callbacks.on(
    ntl::flt::operation::read,
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects objects) noexcept {
      // A: work that is always legal in the native post callback.
      completed.fetch_add(1, std::memory_order_relaxed);

      // B: finish APC-safe work here when possible. Request WhenSafe only
      // when the current IRQL or another runtime condition requires it.
      if (ntl::is_irql_at_most(ntl::irql::apc)) {
        inspect_contexts(objects);
        return ntl::flt::post_continuation::finished;
      }
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::safe_read_operation,
       ntl::flt::related_objects objects) noexcept {
      // NTL dispatches this through FltDoCompletionProcessingWhenSafe.
      // This callback runs at IRQL <= APC_LEVEL.
      inspect_contexts(objects);
    });
```

즉시 작업 후 콜백은 각 완료가 따를 경로를 선택합니다. 필요한 작업을 모두
마쳤거나 안전 콜백 작업이 필요하지 않다면 `post_continuation::finished`를
반환하십시오. NTL이 `FltDoCompletionProcessingWhenSafe`를 호출하게 하려면
`post_continuation::when_safe`를 반환합니다. 이 구조는 모든 완료를 안전 콜백으로
보내지 않고 일반적인 A 실행 후 조건부 B 실행 패턴을 지원합니다. 사용자 콜백은
`post_operation_flags`를 받을 필요가 없습니다. NTL이 트램펄린에서 네이티브
플래그를 확인하고 인스턴스 종료 처리 중에는 두 콜백을 모두 건너뜁니다.

타입이 지정된 완료 상태는 동일한 경로를 따를 수 있습니다.

```cpp
callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    pre_write,
    [](ntl::flt::write_callback_data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_nonpageable_fields(state);
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::safe_post_operation<ntl::flt::operation_id::write>,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_pageable_fields(state);
    });
```

동일한 `completion_ref<T>`는 안전 콜백이 끝날 때까지 유효합니다. NTL은 즉시
콜백이 `finished`를 반환한 뒤, 안전 콜백이 반환한 뒤, 종료 처리 중이거나 필터
관리자가 안전 콜백을 호출하거나 큐에 넣을 수 없을 때 이를 소멸시킵니다.

안전 콜백은 일반 `callback_data`가 아니라 `safe_post_operation<Operation>`을
받습니다. 일반적인 타입이 지정된 상태와 매개변수에 접근하려면 `operation.data()`를
호출하십시오. WhenSafe IRQL 계약이 필요한 API는 이 래퍼만 받으므로 네이티브
작업 후 콜백에서 실수로 호출할 수 없습니다.

드라이버가 `FLT_POST_OPERATION_FLAGS`를 확인하거나 네이티브
`void* CompletionContext`를 관리하거나 네이티브 작업 후 결과를 반환해야 할 때는
플래그 인식 저수준 오버로드를 사용할 수 있습니다. 이 형식에서는 드라이버가 종료
처리를 직접 다뤄야 합니다. `flags.draining()`이 true이면 완료 상태만 해제하고
정상 완료 작업은 건너뛰며 WhenSafe를 요청해서는 안 됩니다.

안전 콜백이 있는 read 및 write 등록에는 NTL이 `skip_paging_io`를 자동으로
설정합니다. paging I/O는 이 방식으로 보낼 수 없기 때문입니다. 즉시 실행되는
콜백이 IRP가 아닌 작업에 WhenSafe를 요청하거나 Filter Manager가 작업을 큐에
넣지 못하면 안전 콜백은 호출되지 않고 NTL이 작업을 완료합니다. 안전 콜백은
`void`를 반환하므로 실수로 완료 처리를 다시 보류할 수 없습니다. 따라서 현재
콜백에서 적법하게 수행할 수 있는 필수 작업은 즉시 완료해야 합니다. Filter Manager가
안전한 연속 처리를 제공하지 못할 때 생략해도 되는 작업에만 안전 콜백을 사용하십시오.

## 타입이 지정된 파일 시스템 컨텍스트

각 상태 형식은 한 번만 선언하고 그 선언을 등록하십시오. 콜백에서 상태를 가져오거나
생성할 때도 같은 선언을 사용합니다.

```cpp
struct file_state {
  std::atomic<std::uint32_t> writes{0};

  file_state() noexcept = default;
  ~file_state() noexcept = default;
};

inline constexpr ntl::flt::file_context<file_state> file_state_context{};

ntl::flt::registration callbacks;
callbacks.context(file_state_context);

// Use this after a successful create, or in another legal <= APC_LEVEL path.
auto state = objects.try_get_or_create(file_state_context);
if (state)
  (*state)->writes.fetch_add(1, std::memory_order_relaxed);
```

공개 선언은 수명 단위를 설명합니다.

| 선언 | 상태는 다음에 속합니다 |
| --- | --- |
| `volume_context<T>` | 하나의 볼륨 |
| `instance_context<T>` | 볼륨에 연결된 하나의 미니필터 인스턴스 |
| `file_context<T>` | 스트림 전체에 공유되는 인스턴스 내 하나의 파일 |
| `stream_context<T>` | 인스턴스 내의 하나의 파일 스트림 |
| `stream_handle_context<T>` | 스트림에 대해 하나의 열린 `FILE_OBJECT` |
| `transaction_context<T>` | 인스턴스 내의 하나의 파일 시스템 트랜잭션 |
| `section_context<T>` | 하나의 필터 관리자 데이터 스캔 섹션(Windows 8+) |

`try_get<T>()`은 `objects.try_get(declaration)`으로 노출됩니다.
`try_get_or_create()`는 `FltGet*Context`, `FltAllocateContext`,
`FltSet*Context(KEEP_IF_EXISTS)`의 전체 절차를 수행합니다. 두 스레드가 같은 객체를
동시에 초기화하려 하면 승자가 설치한 컨텍스트의 참조를 반환하고, 패한 쪽의 할당은
소멸시킵니다. `context_ref<T>`는 이동 전용이며 `FltReleaseContext`를 자동으로
호출합니다. 마지막 참조가 사라지면 Filter Manager가 등록된 cleanup 콜백을
호출하고, 이 콜백이 C++ 컨텍스트 소멸자를 실행합니다.

`objects.try_remove(declaration)`는 설치된 volume, instance, file, stream,
stream-handle 또는 transaction context를 원자적으로 분리하고 제거된 참조를
반환합니다. `context_ref`를 해제하는 것만으로는 컨텍스트가 분리되지 않습니다.

컨텍스트 생성자와 소멸자는 `noexcept`여야 하며, 과도하게 정렬되었거나 크기가
`MAXUSHORT`를 넘는 상태는 컴파일 시 거부됩니다. `volume_context`의 기본 풀은
`context_pool::nonpaged`(`NonPagedPool`)이며, Filter Manager가 이 범위에
허용하는 유일한 풀입니다. 공개 선언은 풀 인수를 받지 않으므로 `PagedPool`이나
`NonPagedPoolNx`를 선택하려 하면 컴파일 오류가 발생합니다. 진단용 풀 태그만
설정할 수 있습니다. 그 밖의 모든 범위는
`context_pool::nonpaged_nx`(`NonPagedPoolNx`)가 기본이며 `nonpaged`,
`nonpaged_nx`, `paged` 중 하나를 명시적으로 선택할 수 있습니다. Filter Manager는
각 필터 인스턴스/객체 관계에 범위별 컨텍스트를 최대 하나만 연결하므로, NTL도
컨텍스트 범위마다 C++ 상태 선언 하나만 허용합니다.

### 트랜잭션 컨텍스트

일반 작업 콜백에서 transaction 상태를 생성하거나 가져온 다음, 마스크로 선택한
알림을 받도록 enlist합니다.

```cpp
auto state = objects.try_get_or_create(transaction_state_context);
if (!state)
  return state.status();

auto status = objects.try_enlist(
    *state, ntl::flt::transaction_notifications::commit_finalize |
                ntl::flt::transaction_notifications::rollback);
```

`registration::on_transaction_notification(transaction_state_context, ...)`으로
등록한 알림 루틴에는 별도의 참조 획득이나 해제 없이, 검증된 비소유
`context_view<T>`가 전달됩니다.

```cpp
ntl::status transaction_notification(
    ntl::flt::related_objects objects,
    ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  state->last_notification = static_cast<ULONG>(notifications);
  return STATUS_SUCCESS;
}
```

정책상 조기 분리가 필요할 때만 `objects.try_remove(transaction_state_context)`를
사용하십시오. 반환된 `context_ref`가 제거한 참조를 소유합니다.

### 데이터 스캔 섹션

section context는 `FltCreateSectionForDataScan`용으로 할당하며
`FltSetXxxContext` 계열 함수로 설치하지 않습니다. 커널이 소유하는 section에는
다음 수명 순서를 사용하십시오.

```cpp
callbacks.on_section_notification(
    section_state_context,
    [](ntl::flt::instance instance,
       ntl::flt::context_view<section_state> state,
       ntl::flt::callback_data_view data) noexcept {
      state->conflict_seen = true;
      return ntl::status::ok();
    });
```

1. `try_register_for_data_scan()`으로 인스턴스를 등록합니다.
2. `section_context<T>`를 할당합니다.
3. `OBJ_KERNEL_HANDLE`를 사용하여 `OBJECT_ATTRIBUTES`를 초기화합니다.
4. `try_create_data_scan_section()`을 호출합니다.
5. 필요에 따라 section을 매핑하고 스캔합니다.
6. `ZwClose()`를 사용하여 반환된 핸들을 닫습니다.
7. `ObDereferenceObject()`로 반환된 section 객체의 참조를 해제합니다.
8. `close_data_scan_section()`으로 Filter Manager 연결을 닫습니다.
9. 남은 `context_ref`를 해제합니다.

```cpp
auto context = ntl::flt::try_allocate_section_context(
    objects.filter().native_handle(), section_state_context);
if (!context)
  return context.status();

OBJECT_ATTRIBUTES attributes;
InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE,
                           nullptr, nullptr);

ntl::flt::data_scan_section_options options;
options.object_attributes = &attributes;

auto section = ntl::flt::try_create_data_scan_section(
    objects.instance(), objects.file(), *context, options);
if (!section)
  return section.status();

// Map and scan here.
(void)ZwClose(section->handle);
ObDereferenceObject(section->object);
return ntl::flt::close_data_scan_section(*context);
```

섹션 충돌 콜백은 `try_create_data_scan_section()`이 반환되기 전에 실행될 수
있으므로, create 호출 전에 섹션 컨텍스트에서 콜백이 참조할 상태를 초기화하십시오.

`OBJ_KERNEL_HANDLE` 없이 생성하면 현재 프로세스에 속한 핸들이 반환됩니다. 이
핸들을 사용자 모드에 전달하면 안전하게 닫을 수 있는 위치와 시점이 달라집니다.
따라서 `data_scan_section`은 자동으로 닫는 RAII 객체가 아니라 네이티브 출력
레코드입니다. 이 순서는 네이티브
[`FltCreateSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcreatesectionfordatascan)과
[`FltCloseSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltclosesectionfordatascan)
계약을 따릅니다.

### Scanner 및 AvScan 수명 주기 조합

section, transaction, 통신, 교체 버퍼, 보류 I/O 및 post-create 취소 API는
의도적으로 서로 독립적입니다. 스캐너 정책은 작업의 수명에 맞춰 이 기능들을
조합합니다.

1. pre-create에서 대상을 선택하고 동기화된 post 콜백을 요청합니다.
2. 성공한 post-create에서 읽기 전용 data-scan section을 통해 검사하고, 앱이
   감염 판정을 반환하면 `try_cancel_file_open()`을 사용합니다.
3. 쓰기 가능한 열기에 `stream_handle_context<T>`를 연결합니다.
4. paging I/O가 아닌 pre-write에서는 작업을 `PASSIVE_LEVEL`로 미루고
   `try_swap_io_buffers()`로 격리된 페이지를 확보한 뒤 그 소유자를
   `pending_pre_operation_queue`로 넘깁니다.
5. 타입이 지정된 driver-to-app 요청을 보냅니다. 허용된 write를 재개하면 큐가
   상주 페이지를 설치하고, 감염된 write는 `STATUS_ACCESS_DENIED`로 취소합니다.
6. pre-cleanup에서 최종 파일 내용을 다시 검사합니다. cleanup을 실패시켜도 mapped
   또는 paging 경로로 이미 기록된 데이터를 되돌릴 수 없으므로, 제품 정책에 따라
   결과를 기록하거나 사후 조치합니다.
7. 트랜잭션 파일에는 `transaction_context<T>`를 생성하고 enlist한 뒤, transaction
   소유권을 wire format에 섞지 않고 commit-finalize와 rollback을 처리합니다.

`swapped_io_buffers`로 확보한 버퍼를 버린 뒤 임의의 worker에서 원래 사용자
포인터로 보류된 write를 재개하면 안 됩니다. 격리 버퍼의 소유자는 보류 큐에
유지하십시오. 허용 경로는 상주 페이지를 하위 스택에 제공하고, 거부 또는 취소
경로는 유효하지 않을 수 있는 사용자 버퍼를 노출하지 않고 페이지를 해제합니다.

서비스 가용성은 별도로 정해야 할 정책입니다. Microsoft Scanner 샘플은 사용자
서비스가 없을 때 서비스와 시스템이 부팅될 수 있도록 fail-open합니다. 이 정책을
따른다면 호출자 버퍼를 매핑·지연·보류하기 전에 연결을 확인하고, 요청을 이미 보류한
뒤 전송에 실패한 경우에도 일관된 방식으로 처리하십시오.

[스캐너/AvScan 런타임 픽스처](../../test/flt/runtime/SCANNER-README.ko-KR.md)는
이 전체 조합을 x64 드라이버, x64 및 WOW64 앱, open/write 허용 및 거부 판정,
mapped-write cleanup 감지, TxF commit/rollback, section 충돌에 안전한 상태 및
균형 잡힌 소유권으로 보여 줍니다.

### Create 시점에 이름 캐시하기

모든 read, write, cleanup 또는 close 콜백에서 이름을 새로 조회하지 마십시오.
성공한 post-create 콜백은 `PASSIVE_LEVEL`에서 실행되므로 최종 정규화 이름을 한 번
조회하고 타입이 지정된 컨텍스트에 보관하기에 알맞습니다.

```cpp
auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                FLT_FILE_NAME_QUERY_DEFAULT);
if (name && name->try_parse().is_ok()) {
  auto file_name = name->try_reference();
  if (file_name) {
    (void)objects.try_get_or_create(file_state_context,
                                    std::move(*file_name));
  }
  (void)objects.try_get_or_create(stream_state_context, std::move(*name));
}
```

`name_information::try_reference()`는 `FltReferenceFileNameInformation`을
호출하므로 각 컨텍스트가 독립적인 RAII 참조를 소유합니다. 파일 컨텍스트는 대체
스트림 사이에 공유되므로 `parent_directory()`와 `final_component()` 같은 파싱된
파일 구성 요소를 사용하십시오. 대체 스트림 구성 요소까지 포함한 완전한 정규화
이름은 스트림 컨텍스트에 보관합니다. 이후 콜백은 새 이름 쿼리 없이 이 컨텍스트를
조회할 수 있습니다. 이는 read, write, pre-cleanup 및 pre-close 경로에서 특히
유용합니다. cleanup 후 새 `FltGetFileNameInformation` 요청은
`STATUS_FLT_INVALID_NAME_REQUEST`로 실패할 수 있지만, 보관한 참조는 컨텍스트가
소멸할 때까지 사용할 수 있습니다.

보관한 정보는 스냅숏일 뿐 rename 알림을 구독한 것은 아닙니다. rename이나 hard link를
추적하는 필터는 해당 set-information 경로에서 컨텍스트를 갱신하거나 무효화해야
합니다. 하위 Filter Manager 이름 정보는 paged pool에서 할당되므로
`IRQL <= APC_LEVEL`에서만 역참조하십시오. 참조를 보관하면 조회를 피할 수는 있지만
paged 메모리를 `DISPATCH_LEVEL`에서 사용할 수 있게 해 주지는 않습니다.

컨텍스트 액세스는 필터 관리자의 기본 제한 사항을 따릅니다.

- 할당, 조회, 설정 및 지원 여부 조회에는 `IRQL <= APC_LEVEL`이 필요합니다.
- file, stream 및 stream-handle context는 pre-create 또는 post-close 콜백에서
  조회하거나 설정할 수 없습니다.
- 페이징 파일 및 일부 타사 파일 시스템은 이를 지원하지 않을 수 있습니다.
  컨텍스트를 지원하지 않을 수 있습니다. `supports()`와 `try_get_or_create()`는
  해당 결과를 그대로 보존합니다.
- `file_context`는 `FltSupportsFileContextsEx`를 사용하므로 필터 관리자의
  단일 스트림 파일 시스템 호환성 경로는 계속 사용 가능합니다.

하위 객체 및 파일 시스템 계약은 Microsoft의
[미니필터 컨텍스트 관리](https://learn.microsoft.com/windows-hardware/drivers/ifs/managing-contexts-in-a-minifilter-driver)와
[파일 시스템의 컨텍스트 지원](https://learn.microsoft.com/windows-hardware/drivers/ifs/file-system-support-for-contexts)을
참고하십시오.

## 타입이 지정된 통신 포트

`ntl::flt::communication_server`는 IOCTL RPC 엔드포인트에서 사용하는 것과 같은
`ntl::rpc::method` 설명자를 Filter Manager 통신 포트로 전달합니다. 설명자는
안정적인 메서드 ID, 인수 형식, 응답 형식, 직렬화된 요청 크기 제한 및 디코드 할당
제한을 정의합니다. 전송 계층은 `FltCreateCommunicationPort`와
`FilterSendMessage`를 그대로 사용합니다.

드라이버와 앱이 공유하는 헤더에 계약을 한 번 선언하십시오. 커널 모드 확장은
콜백을 등록하고 사용자 모드 확장은 `connect()`, `query_count()`,
`query_count_async()`를 생성합니다.

```cpp
NTL_FLT_RPC_BEGIN(product_messages, L"\\ProductPort")

NTL_FLT_ADD_CALLBACK_ID(
    product_messages, 0xA50, std::uint32_t(std::uint32_t), query_count,
    [](std::uint32_t base) noexcept { return base + 1; })

NTL_FLT_RPC_END(product_messages)
```

필터를 시작하기 전에 생성된 서버를 등록하십시오.

```cpp
auto messages = product_messages::make_server();
auto status = driver.add_communication_port(product_messages::port_name,
                                             std::move(messages));
if (status.is_err())
  return status;

return driver.start(std::move(callbacks));
```

사용자 모드 클라이언트는 동일한 설명자를 사용합니다.

```cpp
auto client = product_messages::connect();
const auto answer = product_messages::query_count(client, std::uint32_t{41});
```

클라이언트 인수를 명시적으로 받는 것은 의도된 설계입니다. 생성된 함수가 호출될
때마다 몰래 다시 연결하는 대신, 호출·공유 영역·연결 상태가 모두 같은 Filter Manager
연결에 유지됩니다.

### 비동기, 취소 및 코루틴

`FilterSendMessage` 자체는 동기 방식입니다. 따라서 NTL의 비동기 메서드는 크기가
제한된 요청을 제출할 때만 이 함수를 사용합니다. 미니필터는 `PASSIVE_LEVEL` worker에서
콜백을 실행하고 `FltSendMessage`로 결과를 보냅니다. 개수가 제한된 overlapped
`FilterGetMessage` 수신기가 고정 폭 요청 ID를 기준으로 완료를 전달합니다. 한
클라이언트에 여러 호출이 동시에 보류될 수 있지만,
`communication_port_options::max_pending_async()`가 연결별 보류 작업 수와 복사된
요청 메모리를 각각 제한합니다. 기본값은 64입니다.

커널 비동기 콜백과 driver-to-app 전달은 원시 executive work item이 아니라
`queue_filter_work_item`으로 큐에 넣습니다. Filter Manager는 삭제가 시작된 뒤의
새 작업을 거부하고, 성공적으로 큐에 들어간 각 콜백이 실제로 반환할 때까지 필터의
rundown 참조를 유지합니다.

```cpp
auto first = product_messages::query_count_async(client, 40);
auto second = product_messages::query_count_async(client, 41);

const auto first_result = first.get();
const auto second_result = second.get();
```

`ready()`, `wait_for()`, `wait()`, `cancel()`, `get()`은
`ntl::rpc::async_call`과 같은 단일 결과 소유권 모델을 따릅니다. 취소는 협력적입니다.
`cancel()`은 커널 요청에 취소를 표시하고, 오래 실행되는 콜백이
`communication_context::cancellation_requested()`를 확인합니다. 완료가 취소와의
경합에서 이길 수 있으므로, 취소 요청이 이미 반영된 작업까지 되돌린다고 가정하면 안
됩니다.

C++20에서는 `<ntl/flt/coroutine>`를 포함하고 생성된 비동기 호출을
`co_await`로 이동하십시오.

```cpp
task<std::uint32_t> query(ntl::flt::communication_client& client) {
  co_return co_await product_messages::query_count_async(client, 41);
}
```

생성된 `_async` 함수는 C++20에서 `std::stop_token`도 받습니다. 이 토큰 역시
같은 협력적 취소를 요청합니다. C++14 이상에서는 일반 동기 API와 명시적 비동기 호출
API를 계속 사용할 수 있습니다.

드라이버는 이름이 서로 다른 포트를 여러 개 열 수 있습니다. 각 연결은 고정 메모리
할당량과 메서드 디스패치를 독립적으로 갖습니다. NTL은 `FltUnregisterFilter`보다
먼저 listener를 닫습니다. 이후 Filter Manager가 남아 있는 모든 클라이언트의
disconnect 콜백을 호출하고 나서 서버 상태가 소멸합니다.

### 계약, 세션, 알림 및 스트림

앱과 미니필터가 공유 계약을 검증해야 한다면 `NTL_FLT_RPC_BEGIN_CONTRACT`를
사용하십시오. `connect()`가 엔드포인트를 조회하고, 첫 메서드 호출 전에 애플리케이션
버전이 다르거나 필요한 capability bit가 없으면 연결을 거부합니다.

계약 조회는 app-to-driver 및 driver-to-app 메서드, 알림, 스트림을 모두 알립니다.
각 항목에는 고정 폭 요청·응답·디코드 할당·배치·우선순위 제한과 자동으로 계산한
wire schema 지문이 포함됩니다. 공유 설명자가 실행 중인 드라이버와 맞지 않으면
정상 트래픽을 시작하기 전에 앱에서 실패를 확인할 수 있습니다.

```cpp
auto contract = client.query_contract();
client.require_method(product_messages::query_count_method);
client.require_client_method(product_messages::app_transform_method);
client.require_notification(product_messages::progress_notification);
client.require_stream(product_messages::numbers_stream);
```

명시적인 애플리케이션 계약 버전은 API 의미나 수명 주기의 변경을 식별합니다. NTL은
직렬화된 필드 형식으로 각 구성원의 지문을 계산하고, 타입이 지정된 해당 구성원을
사용할 때 검증합니다. 사용자 정의 형식은 C++14에서도 기존
`static serialize(Archive&, Self&)` 필드 목록을 재사용합니다.
컴파일러 형식 이름은 x86, x64 또는 MSVC 도구 집합 간에 안정적이지 않으므로
지문에서 제외됩니다.

### 연결 및 드라이버-앱 요청

`communication_server::on_connect()`는 포트를 사용할 수 있게 되기 전에 클라이언트를
허용하거나 거부하고, 타입이 지정된 애플리케이션 상태를 연결할 수 있습니다.
`on_disconnect()`는 확정된 연결 해제를 관찰합니다. 복사한
`communication_connection`은 연결이 끊긴 뒤에도 안전하게 검사할 수 있습니다.
`connected()`가 false가 되고, 대상 작업은 이전 Filter Manager 포트를 역참조하는
대신 실패합니다.

```cpp
struct client_state {
  std::uint32_t accepted_calls = 0;
};

server
    .on_connect([](ntl::flt::communication_connection& connection) {
      connection.state(std::make_shared<client_state>());
      return ntl::status::ok();
    })
    .on_disconnect([](ntl::flt::communication_connection& connection) {
      connection.clear_state();
    });
```

같은 포트를 양방향으로 사용할 수 있습니다. 서버를 시작하기 전에 드라이버에서 시작할
메서드를 등록하고, 앱에 해당 핸들러를 등록한 뒤 현재 콜백과 연결된 connection으로
호출하십시오.

```cpp
// Driver setup and callback path.
server.register_client_method(product_messages::app_transform_method);
auto result = publisher.try_request(
    context.connection(), product_messages::app_transform_method,
    std::chrono::seconds(2), value);

// App setup before it invokes the driver path that makes the request.
client.on_request(product_messages::app_transform_method,
                  [](std::uint32_t value) noexcept { return value * 3; });
```

앱은 네 개의 overlapped `FilterGetMessage` 수신을 보류 상태로 유지합니다. 하나의
수신 경로에서 비동기 메서드 완료, 알림, 스트림 레코드 및 드라이버가 시작한 요청을
디스패치합니다. 따라서 앱 요청 핸들러는 동시에 실행될 수 있으며 공유 상태를 적절히
보호해야 합니다.

각 연결은 하나의 서버 세션을 엽니다. 일시적 알림은 best-effort 방식이며 ACK 없이
소비됩니다.

```cpp
auto wait = product_messages::progress_async(client);
// The driver calls publisher.try_notify(progress_notification, event).
auto event = wait.get();
```

`publisher.try_notify(notification, payload)`는 현재 구독자들에게 브로드캐스트합니다.
`publisher.try_notify(connection, notification, payload)`는 연결된 구독자 하나를
대상으로 합니다. 연결이 끊겼거나 해당 채널을 구독하지 않았다면 대상 전송은
`STATUS_NOT_FOUND`를 반환하며, 암묵적으로 브로드캐스트로 바뀌지 않습니다.

신뢰성 알림은 앱이 해당 시퀀스를 ACK할 때까지 세션의 크기 제한 큐에 남습니다.
`max_reliable_records()`와 `max_reliable_bytes()`는 각 세션에 독립적으로
적용되므로 멈춘 앱이 커널 메모리를 무한히 늘릴 수 없습니다.

```cpp
auto delivery = product_messages::progress_reliable(client);
process(delivery.payload());
client.acknowledge(product_messages::progress_notification, delivery);
```

일반적인 핸들 또는 프로세스 종료는 세션을 제거합니다. ACK하지 않은 신뢰성 레코드를
잃지 않고 다시 연결하려면 먼저 세션을 명시적으로 분리하십시오. 이 작업은 현재
클라이언트를 무효화하고 보류 작업을 취소한 뒤 `resume()`에 사용할 토큰을 반환합니다.

```cpp
auto token = client.preserve_session();
auto resumed = product_messages::resume(token);
auto replayed = product_messages::progress_reliable(resumed);
resumed.acknowledge(product_messages::progress_notification, replayed);
```

외부 저장소를 명시적으로 설치하지 않으면 신뢰성 큐는 미니필터가 로드된 동안에만
유지됩니다. 사용자 정의 저장소는 `communication_notification_store`를 상속해
구현할 수 있습니다. NTL은 선택적으로 `registry_notification_store`도 제공합니다.

```cpp
auto key = ntl::registry_key::create(
    L"\\Registry\\Machine\\Software\\ProductFilterNotifications",
    KEY_QUERY_VALUE | KEY_SET_VALUE);
if (!key)
  return key.status();

messages.notification_storage(
    std::make_shared<ntl::flt::registry_notification_store>(
        std::move(*key)));
```

이 저장소는 다시 연결할 수 있는 세션마다 크기가 제한된 `REG_BINARY` 값 하나를
유지합니다. NTL은 레코드를 공개하기 전에 `persist()`를 호출하고, 명시적인 ACK 뒤에
레코드를 제거하며, 메모리에 세션 토큰이 없을 때 `restore()`를 호출합니다. 저장소를
설치하지 않으면 저장소 I/O는 전혀 발생하지 않습니다.

저장소 훅은 NTL의 connection 또는 session lock을 잡지 않은 채 `PASSIVE_LEVEL`에서
실행되며 스레드 안전해야 합니다. `communication_record_view::data` 범위는 훅 호출
중에만 유효합니다. 배치 전송에 사용하는 저장소는 `acknowledge_batch()`를 원자적
작업으로 재정의해야 합니다. NTL은 외부에 보이는 단일 레코드 커밋 여러 번으로 배치를
흉내 내지 않습니다. 세션을 명시적으로 닫으면 `erase_session()`을 호출합니다.

신뢰성 수신은 크기가 제한된 배치를 요청할 수 있습니다.

```cpp
auto wait = client.receive_reliable_batch_async(
    product_messages::progress_notification);
auto batch = wait.get();
consume(batch.values());
client.acknowledge(product_messages::progress_notification, batch);
```

`max_reliable_records()`와 `max_reliable_bytes()`는 각 세션을 제한합니다.
`reliable_overflow(reject_newest)`는 큐의 데이터를 보존하고 새 레코드를 거부합니다.
`reliable_overflow(drop_oldest)`는 전송 중이 아닌 가장 오래된 레코드를 제거합니다.
후자는 외부 저장소가 설치됐을 때 의도적으로 사용할 수 없습니다. 애플리케이션 저장소가
제거를 ACK와 동일하게 처리한다고 NTL이 가정할 수 없기 때문입니다.

타입이 지정된 스트림은 크기가 제한된 app-to-driver 업로드 메서드 하나와 순서 및
ACK가 보장되는 driver-to-app 채널 하나를 결합합니다. 업로드 배치는 크기가 제한된
벡터를 한 요청으로 직렬화하며, 모든 다운로드 레코드는 ACK될 때까지 유지됩니다.

```cpp
auto stream = product_messages::numbers(client);
stream.write_batch(std::vector<std::uint32_t>{10, 20});

for (int index = 0; index != 2; ++index) {
  auto reply = stream.read();
  consume(reply.payload());
  stream.acknowledge(reply);
}
stream.close();
```
하나의 필터 관리자 메시지로 설명자에 지정된 최대 배치 크기까지 수신하고,
전송 작업 한 번으로 그 배치를 확인 응답하려면 `read_batch()` 또는
`read_batch_async()`를 사용하십시오. 배치에는 레코드가 하나만 들어 있을 수도
있으므로, 호출자는 동시 쓰기가 항상 하나로 합쳐진다고 가정하면 안 됩니다.

드라이버 콜백은 `communication_stream_context<Stream>`을 받습니다. 이 객체의
`try_write()`, `try_complete()`, `try_fail()` 메서드는 데이터 또는 종료 레코드를
같은 크기 제한 신뢰성 큐에 넣습니다. 채널 간 우선순위가 다음에 전송할 큐를 선택하되,
한 스트림 안의 레코드는 순서와 ACK 계약을 유지합니다.

알림 대기, 신뢰성 대기, 스트림 쓰기 및 스트림 읽기는 동기와 비동기 형식을 모두
제공합니다. C++20에서는 비동기 값을 `co_await`할 수 있고 `std::stop_token`에
연결할 수 있습니다. C++14 이상에서는 명시적인 wait/cancel/get API를 계속
사용합니다.

### 권한 부여 및 리소스 제한

`communication_port_options`는 connection, 보류 비동기 호출, 보관된 session,
신뢰성 레코드/바이트 및 pinned region 수를 제한합니다. 기본 ACL이 제품 정책에
맞지 않으면 Filter Manager 포트에 사용자 정의 보안 설명자를 설정하십시오. 메서드별
정책에는 `on_authorized()`와
`NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID`는 요청 역직렬화 전에 실행됩니다.

```cpp
NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID(
    product_messages, 0xA20, reply_type(request_type), privileged_call,
    [](const ntl::flt::communication_context& context) noexcept {
      return authorize_process(context.requestor_process_id());
    },
    [](request_type request) { return handle(request); })
```

정책은 `NTSTATUS` 또는 `ntl::status`를 반환합니다. 캡처된 원래 요청자 프로세스
ID 및 `session_id()`를 일반 Windows 커널 보안 API와 함께 사용할 수 있습니다.
권한 검사는 serializer 할당 전에 수행되므로 거부된 호출자는
보호된 메서드가 공격자가 제어하는 컨테이너를 디코딩하도록 강제할 수 없습니다.

### 공유 영역과 32/64비트 상호 운용

크거나 자주 교환하는 고정 레이아웃 데이터에는 매 레코드를 직렬화하는 대신 연결에
묶인 공유 영역을 사용할 수 있습니다. 앱이 영역을 할당해 등록한 뒤 일반적인 형식
지정 메서드로 `ntl::ipc::buffer_token` 값을 보냅니다.

```cpp
auto region = client.register_shared_region(bytes);
auto input = region.token(0, input_bytes);
auto output = region.token(output_offset, output_bytes);
client.invoke(process_buffers, input, output);
```

커널 콜백은 필요한 액세스 권한으로 각 토큰을 확인합니다.

```cpp
messages.on(process_buffers,
    [](const ntl::flt::communication_context& context,
       ntl::ipc::buffer_token input,
       ntl::ipc::buffer_token output) {
      auto readable = context.try_resolve(
          input, ntl::ipc::region_access::driver_read);
      auto writable = context.try_resolve(
          output, ntl::ipc::region_access::driver_write);
      // Attach an ntl::ipc::shared_ring or validate another fixed layout.
    });
```

토큰에는 프로세스 포인터가 아니라 고정 폭 영역 ID, 오프셋 및 길이 필드가 들어
있습니다. 따라서 같은 프로토콜로 x86 앱을 x64 미니필터에 연결할 수 있습니다.
토큰을 실제 범위로 변환할 때는 pinned range를 노출하기 전에 connection 소유권,
generation, 범위 및 접근 권한을 검사합니다. 토큰을 사용하는 모든 동기 또는 비동기
호출이 완료된 뒤에만 `registered_port_region`을 소멸시키거나 `close()`하십시오.
등록을 취소하면 앱이 가상 할당을 해제하기 전에 MDL이 해제됩니다.

가변 크기 payload에는 `registered_port_region::make_buffer_pool()`로
`ntl::ipc::shared_buffer_pool`을 만들 수 있습니다. 이동 전용 lease가 하위 범위를
예약하고 소멸할 때 풀에 반환합니다. 드라이버는 여전히 일반 `buffer_token`을 받아
검증합니다. 등록한 포트 영역은 모든 lease보다 오래 살아야 합니다. 런타임 픽스처는
미니필터 어댑터를 통해 할당, 해제, 재사용 및 병합을 검사합니다.

메시지 콜백은 `PASSIVE_LEVEL`에서 실행됩니다. NTL은 Filter Manager의 정렬되지
않은 사용자 버퍼를 디코드 전에 구조적 예외 처리 아래서 복사하고, 메서드별 요청 및
할당 제한을 적용합니다. 올바른 연결을 끊지 않고도 잘못된 framing이나 오래된 영역
토큰을 거부합니다. 런타임 픽스처는 형식 지정 호출, 동시 비동기 완료 디스패치,
협력적 취소, 공유 ring, 잘못되거나 과도하게 큰 framing, 유효하지 않은 공유 영역의
범위·접근·할당량 토큰, 오래된 토큰 거부 및 거부된 요청 이후의 연결 재사용을
검사합니다. 고급 통신 테스트는 형식 지정 connection 상태, connect/disconnect 관찰,
연결 거부, connection/session 할당량, driver-to-app 요청, 대상 전송의 구독 확인,
상세 계약 불일치, 신뢰성 레코드/바이트 할당량, 신뢰성 배치 ACK와 중복 거부, 외부
복원 훅, drop-oldest overflow, 스트림 실패 완료, C++20 코루틴 호출 및 동시
connect/call/close 스트레스도 검사합니다. 이 테스트 전용 경로는 입문 샘플에는
포함하지 않습니다.

## IRQL 및 수명

항상 콜백의 네이티브 Filter Manager 계약을 우선해야 합니다.

- 드라이버 진입, 등록, instance setup 및 unload는 `PASSIVE_LEVEL` 경로입니다.
- 일부 pre-operation 콜백은 `APC_LEVEL`에서 실행될 수 있습니다.
- post-operation 콜백은 `DISPATCH_LEVEL`에서 실행될 수 있습니다. 이 경로에서는
  pageable 코드, 블로킹 작업, 예외 및 일반 CRT/STL 작업을 사용하지 마십시오.
- `try_query_name()`이 이름 조회를 항상 적법하게 만들어 주지는 않습니다.
  `FltGetFileNameInformation`이 허용되는 I/O 경로에서만 호출하십시오.
- 문서에서 RAII라고 명시하지 않은 뷰는 비소유입니다. 콜백이 끝난 뒤 콜백 데이터나
  related-object 뷰를 보관하지 마십시오.

빌드 가능한 [미니필터 샘플 목록](../../examples/minifilter)은 입문 단계에서 알아야 할
여섯 가지 주제를 분리해 보여 줍니다.

- [`basic`](../../examples/minifilter/basic)은 형식 지정 create/read/write/cleanup
  콜백과 캐시된 스트림 이름 컨텍스트를 다룹니다.
- [`control-device`](../../examples/minifilter/control-device)는 WDK CDO 수명 주기를
  형식 지정 장치/IOCTL 핸들러 및 unload 거부로 대응시킵니다.
- [`communication`](../../examples/minifilter/communication)은 Filter Manager 포트,
  형식 지정 RPC, 알림, 스트림 및 공유 ring 동작을 별도로 다룹니다.
- [`operation-log`](../../examples/minifilter/operation-log)는 MiniSpy 흐름을 형식 지정
  콜백, 크기 제한 큐 및 형식 지정 사용자 모드 drain 처리로 대응시킵니다.
- [`swap-buffers`](../../examples/minifilter/swap-buffers)는 데모 변환을 위해 pre-WRITE
  입력과 pre-READ 출력을 교체합니다.
- [`volume-metadata`](../../examples/minifilter/volume-metadata)는
  MetadataManager 수명 주기를 타입이 지정된 볼륨 잠금, 스냅샷, PnP, 종료 및
  해제 처리로 대응시킵니다.

카탈로그는 읽기 쉬운 각 예제를 대응하는
[런타임 픽스처](../../test/flt/runtime)에 연결합니다. 이 픽스처들은 대규모 실패,
파일 시스템, WOW64 및 Driver Verifier 매트릭스를 유지합니다. 특히 NameChanger는
create 리디렉션, 이름 공급자 콜백, 디렉터리 열거, 정보 쿼리, 이름 변경/하드 링크
대상, 알림 및 이름을 포함하는 FSCTL 결과가 하나의 교차 계약을 이루므로 픽스처에
남겨 둡니다. 일부만 떼어내면 읽기는 쉬워도 올바른 NameChanger 구현이 되지
않습니다.

마이크로소프트 참고자료:

- [미니필터 드라이버용 INF 파일 생성](https://learn.microsoft.com/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver)
- [로드 순서 그룹 및 고도](https://learn.microsoft.com/windows-hardware/drivers/ifs/load-order-groups-and-altitudes-for-minifilter-drivers)
- [FltAttachVolume](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltattachvolume)
