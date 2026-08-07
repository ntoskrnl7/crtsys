# 미니필터 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::flt`는 Windows 파일 시스템용 NTL 항목 및 콜백 계층입니다.
미니 필터. 필터 관리자는 여전히 필터 등록, 인스턴스 주문,
I/O 콜백 전달 및 해제. NTL은 형식화된 비소유 파사드와
등록된 필터에 대해 콜백 테이블을 활성 상태로 유지합니다.

읽을 수 있는 내용은 [미니필터 샘플 카탈로그](../../examples/minifilter)를 참조하세요.
드라이버/앱 예시. WDK 샘플에서 확인된 NTL로의 저장소 매핑
메커니즘은
[WDK 미니필터 샘플 적용 범위 매트릭스](../../test/flt/WDK-SAMPLE-COVERAGE.ko-KR.md).

## 참가 및 등록

`ntl::flt::main`를 정의하고 등록을 드라이버로 이동합니다.

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

`ntl::flt::operation`는 원시를 노출하지 않고 필터 관리자 작업의 이름을 지정합니다.
`IRP_MJ_*` 값. 사전 작업 전용 등록은 플래그를 직접 전달할 수 있습니다.

```cpp
callbacks.on(ntl::flt::operation::write, pre_write,
             ntl::flt::operation_flags::skip_paging_io);
```

`nullptr` 작업 후 자리 표시자는 필요하지 않습니다.
`operation::read` 및 `operation::write`에만 허용되는 오버로드가 있습니다.
`operation_flags`. 다른 작업으로 읽기/쓰기 전용 플래그를 전달하는 것은
런타임 등록 실패가 아닌 컴파일 타임 오류입니다.

CMake의 경우 미니필터 모델을 명시적으로 선택합니다.

```cmake
crtsys_add_driver(my_filter MINIFILTER NTL driver/main.cpp)
```

NuGet 패키지를 사용하는 Visual Studio WDK 프로젝트의 경우:

가장 쉬운 설정은 **프로젝트 속성 > 드라이버 설정 > 드라이버 모델**입니다.
그런 다음 **crtsys WDM 진입점**을 **NTL Minifilter**로 설정합니다. 패키지는 다음을 씁니다.
MSBuild 대상을 통해 아래의 두 가지 속성:

```xml
<DriverType>WDM</DriverType>
<CrtSysIsMinifilter>true</CrtSysIsMinifilter>
<CrtSysUseNtlFltMain>true</CrtSysUseNtlFltMain>
```

이 설정은 `CrtSysNtlFltDriverEntry`를 선택하고 crtsys를 초기화합니다.
런타임, `fltmgr.lib` 연결, `ntl::flt::main` 호출 및 필터 등록 취소
런타임 해체 전.

### 사전 구축된 ABI 및 Windows 대상 버전

`crtsys.lib`는 Windows 8 필터 관리자 선언으로 구축되었지만
`ntl::flt`는 Windows 7 소비자를 지원합니다. 공개 `ntl::flt::driver` 및
따라서 `ntl::flt::registration` 레이아웃은 버전 조건을 포함하지 않습니다.
WDK 스토리지:

- Win8 전용 섹션 콜백 슬롯은 동일한 삭제된 저장소를 예약합니다.
  모든 Windows 7+ 빌드(등록 API는 다음 경우에만 노출됨)
  `FLT_MGR_WIN8`를 사용할 수 있습니다.
- `FLT_REGISTRATION`는 소비자 번역 장치에 의해 간접적으로 할당되며,
  해당 소비자의 `sizeof(FLT_REGISTRATION)`로 채워지고
  `FLT_REGISTRATION_VERSION`이며 소비자 소유 삭제 프로그램을 통해 파괴됩니다.

결과적으로 Windows 7 미니필터 프로젝트는 일치하는 사전 빌드된 항목을 연결할 수 있습니다.
C++ 멤버 오프셋을 변경하지 않는 NuGet 라이브러리, Windows 8+ 프로젝트
여전히 섹션 알림 필드를 받습니다. 컴파일/링크 테스트 매트릭스
`0x0601` 및 `0x0602`를 사용하여 양면을 구축하고 두 공용 객체를 모두 인코딩합니다.
하나의 필수 템플릿 기호에 크기가 표시됩니다. 미래의 조건부 레이아웃 회귀
따라서 언로드 중에 개체가 손상되는 대신 빌드가 실패합니다.

## 레거시 제어 장치

미니필터는 필터 외에 일반 WDM 제어 장치를 노출시킬 수 있습니다.
관리자 통신 포트. `driver::add_control_device()`는 다음을 연결합니다.
기존 유형의 `ntl::device` 및 `ntl::ioctl` 시설을 미니필터에 추가
진입, 시작, 실패 및 언로드 수명:

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

짧은 이름은 사용자 모드가 열리는 `\\DosDevices\\ProductControl`를 게시합니다.
`\\.\ProductControl`로. 선택적 세 번째 인수는 완전한 DOS를 제공할 수 있습니다.
링크 이름. 제품 소유 제품과 함께 `device_options::security_descriptor()`를 사용하세요.
설치 과정에서 이름 있는 legacy 장치의 ACL을 제공하지 않는다면 제품이 소유한
setup-class GUID와 함께 `device_options::security_descriptor()`를 사용하세요.

등록 요청은 `start()` 전까지 대기합니다. NTL은 `FltRegisterFilter`를 호출하고
장치를 생성·구성한 뒤 link를 게시하고 `FltStartFiltering`을 호출합니다. 시작이
실패하거나 unload를 허용하면 장치를 삭제하기 전에 symbolic link를 제거합니다.
드라이버 source는 `DriverObject->MajorFunction`을 직접 설정하거나 `fltKernel.h`를 포함하지
않습니다. NTL 미니필터 진입점이 create, cleanup, close 및 device-control IRP를
형식화된 핸들러로 전달합니다.

열린 CDO가 있으면 선택적 미니필터 언로드를 거부해야 할 수 있습니다. 이 정책은
장치 상태에서 추적하고, `registration::on_unload`에서
`!flags.mandatory()`인 동안 `STATUS_FLT_DO_NOT_DETACH`를 반환하십시오. 언로드
콜백이 요청을 수락한 뒤에는 `ntl::flt::driver`가 엔드포인트 해체를 담당하므로
장치를 별도로 삭제하면 안 됩니다.

[CDO 런타임 픽스처](../../test/flt/runtime/CDO-README.ko-KR.md)는
사용자 모드 열기, 단일 열기 정책, 형식화된 IOCTL, 실제 선택적 언로드 거부,
거부 후에도 계속되는 디스패치, cleanup/close, 다시 열기 및 최종 언로드를
검증합니다.

## 인스턴스 및 고도

등록된 필터와 연결된 인스턴스는 수명 단위가 다릅니다.
INF는 하나 이상의 명명된 인스턴스 구성을 정의합니다.
고도 및 부착 플래그. 그런 다음 필터 관리자는 별도의
하나의 구성이 볼륨에 연결될 때마다 `PFLT_INSTANCE`입니다. 같은
따라서 기본 정의는 여러 런타임 인스턴스를 생성할 수 있습니다.
볼륨은 명시적으로 선택된 여러 정의를 호스팅할 수 있습니다.
다른 고도.

고도는 설치 메타데이터이며 `registration::on()` 또는
`driver::start()`. 하위 수준 모두에서 프로덕션 인스턴스를 정의합니다.
`Instances` 및 Windows 11 24H2 `Parameters\Instances` INF 레이아웃:

```ini
HKR,"Parameters\Instances","DefaultInstance",0x00000000,%DefaultInstance%
HKR,"Parameters\Instances\%DefaultInstance%","Altitude",0x00000000,%DefaultAltitude%
HKR,"Parameters\Instances\%DefaultInstance%","Flags",0x00010001,0
HKR,"Parameters\Instances\%SecondaryInstance%","Altitude",0x00000000,%SecondaryAltitude%
HKR,"Parameters\Instances\%SecondaryInstance%","Flags",0x00010001,1
```

`Flags=0`는 자동 부착을 허용합니다. `Flags=1`는 자동을 억제합니다.
첨부 파일을 사용하면 명명된 정의를 명시적으로 선택할 수 있습니다. 진짜
제품은 Microsoft의 미니필터에 따라 할당된 고도를 사용해야 합니다.
고도 정책; 샘플의 값은 개발 전용입니다.

콜백 내부에서 `objects.instance()`는 정확한 첨부 파일을 식별합니다.
결과적으로 `instance_context<T>`는 각 항목에 대해 별도의 상태를 저장합니다.
필터/볼륨/고도 부착. 안정적인 신원을 쿼리하세요.
진단 또는 정책에 필요한 경우 `PASSIVE_LEVEL`:

```cpp
auto information = objects.instance().try_information();
if (information) {
  // information->name, altitude, volume_name, and filter_name are owning
  // strings and remain valid after the Filter Manager query buffer is freed.
}
```

이미 `ntl::flt::volume`을 소유한 커널 코드는 필터 퍼사드를 통해 명시적으로
연결을 관리할 수 있습니다.

```cpp
auto attached = driver.filter().try_attach(volume, L"Product Secondary");
if (!attached)
  return attached.status();

auto identity = attached->view().try_information();
// instance_ref releases the rundown reference; detaching is a separate action.
driver.filter().try_detach(volume, L"Product Secondary");
```

`try_attach()`는 설치된 INF에서 명명된 정의의 속성을 읽습니다.
`try_attach_at()`는 명시적인 진단 배치에 사용할 수 있지만 이름은 INF입니다.
정의는 정상적인 생산 계약입니다. `try_instances()` 열거
필터가 소유한 모든 현재 첨부 파일. 이 도우미는 STL 소유를 사용합니다.
문자열과 벡터이므로 NTL 계약은 `PASSIVE_LEVEL`입니다.
기본 필터 관리자 쿼리는 `APC_LEVEL`를 허용합니다.

기본 필터 관리자 등록은 다음과 같은 경우 수동으로 분리할 수 없습니다.
`InstanceQueryTeardownCallback`는 null입니다. NTL은 다음을 통해 허용 콜백을 등록합니다.
기본값이므로 `FilterDetach`, `FltDetachVolume` 및 `filter::try_detach()`가 작동합니다.
빈 사용자 콜백 없이. 다음과 같은 경우에는 `on_instance_query_teardown()`를 사용하십시오.
드라이버는 미해결된 인스턴스별 작업을 검사하고 가능한 경우 반환해야 합니다.
`STATUS_FLT_DO_NOT_DETACH`. 무조건적으로 `deny_manual_detach()`를 사용하세요.
정책 거부; `allow_manual_detach()`는 NTL 기본값을 복원합니다.

### 볼륨별 메타데이터 조정

`volume_metadata_file`는
`volume_metadata_instance_context<T>`. 메타데이터 핸들을 소유하고 있으며
참조된 파일 객체는 암시적 또는 명시적 볼륨 이전에 해당 객체를 닫습니다.
잠금, 마운트 해제 또는 쿼리 제거 및 동일한 트리거에 대해서만 다시 열기
볼륨 `FILE_OBJECT`:

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

특수화된 컨텍스트는 항상 `NonPagedPoolNx`를 사용하며 의도적으로
풀 선택 생성자. 이는 `volume_metadata_file` 때문에 필요합니다.
저장소가 상주해야 하는 `ERESOURCE` 및 `KEVENT` 개체를 포함합니다.
일반 페이징 컨텍스트나 기타 페이징 할당에 배치하지 마세요.

`ntl::file::is_volume_open()`는 볼륨 핸들을 식별합니다.
`create_parameters::is_implicit_volume_lock_candidate()`,
`file_system_control_parameters::volume_request()` 및
`pnp_parameters::request()`는 해당 유형의 콜백을 분류합니다.
잠금/마운트 해제/쿼리 제거 전에 `try_release_for(objects.file())`를 호출하고
잠금/마운트 해제 실패 후 `try_reopen_for(objects.file())`, 성공
잠금 해제, 암시적 잠금 정리 또는 취소-제거.

명시적 잠금 해제에 성공하면 이전 볼륨 인스턴스가 무효화될 수 있습니다.
`try_reopen_for()`는 합법적으로 반환됩니다.
`STATUS_INVALID_DEVICE_OBJECT_PARAMETER`, `STATUS_FILE_INVALID` 또는
`STATUS_NO_MEDIA_IN_DEVICE`; 다시 마운트된 볼륨의 인스턴스 설정은 다음과 같습니다.
새로운 메타데이터 소유자를 여는 일을 담당합니다. 코드는 이를 해석해서는 안 됩니다.
메타데이터가 영구적으로 손실되었다는 증거로 간주됩니다.

스냅샷 조정에는 작업 전 콜백부터 다른 작업 후 스레드까지 `ERESOURCE`를 계속
보유하는 대신, 스레드 간에 안전하게 이동할 수 있는 이동 전용 토큰을 사용합니다.
`try_begin_update()`는 업데이트를 승인합니다.
`try_hold_updates_for_snapshot()`은 새 업데이트를 차단하고 이미 승인된 작업이
끝나기를 기다리며, 작업 후 콜백에서 토큰이 파괴되면 업데이트를 재개합니다.
`device_control_parameters::is_snapshot_flush_and_hold()`는 스냅샷 요청을
식별합니다.

[MetadataManager 런타임 픽스처](../../test/flt/runtime/METADATA-README.ko-KR.md)는
잠금 해제로 인한 재마운트 및 성공을 포함하여 ReFS에서 이러한 경로를 확인합니다.
분리/분리/다시 마운트.

## I/O 작업 이상의 등록 콜백

`registration`는 현재 비작업 콜백 슬롯을 노출합니다.
형식화된 콜백 서명을 통한 `FLT_REGISTRATION`. 원시 `FLTAPI`, `PFLT_*`,
및 `PVOID*` 매개변수는 NTL의 기본 트램폴린 내부에 유지됩니다.

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

트랜잭션 및 섹션 오버로드는 제공된 컨텍스트를 등록합니다.
선언하고 해당 C++ 상태 유형을 콜백에 바인딩합니다. 그것도 통과하지 마세요
`registration::context()`에 선언합니다. 버전에 따른 콜백은 다음과 같습니다.
선택한 WDK가 지원하는 경우에만 노출됩니다.

`name_control`는 파일 이름 생성 콜백에 전달된 출력을 래핑합니다.
`try_assign()` 및 `try_append()`는 이를 통해 성장합니다.
`FltCheckAndGrowNameControl`; 필터 관리자는 계속해서 버퍼를 소유합니다.

### 이름 공급자 출력

`name_generation_request`에는 인스턴스, 파일, 선택 사항이 포함됩니다.
작업에 구애받지 않는 `callback_data_view` 및 구문 분석된 옵션 보기.
`name_generation_output`에는 소유하지 않은 `name_control` 및 캐시가 포함되어 있습니다.
결정:

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

이름 공급자는 일반적으로 아래 공급자가 보고한 이름이 필요합니다.
해당 조회에는 `request.try_query_lower_name(options)`를 사용하세요. 그것은 선택한다
콜백 데이터가 있는 경우 `FltGetFileNameInformation`
그렇지 않으면 `FltGetFileNameInformationUnsafe`이며 항상 제거됩니다.
공급자 재귀를 방지하기 위한 `FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER`:

```cpp
auto lower = request.try_query_lower_name(
    FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
    FLT_FILE_NAME_DO_NOT_CACHE);
if (!lower)
  return lower.status();
if (auto status = lower->try_parse(); status.is_err())
  return status;
```

등록 콜백은 `related_objects`를 수신하지 않습니다. 언제 그들의
볼륨별 정책은 `instance_context<T>`에 있으며 다음에서 직접 검색합니다.
형식화된 인스턴스:

```cpp
auto mapping = request.target_instance().try_get(mapping_context);
if (!mapping)
  return mapping.status();
```

`name_normalization_request`는 마찬가지로 인스턴스, 선택적 파일,
상위 디렉터리, 볼륨 접두사, 구성 요소 및 정규화 플래그.
`name_normalization_output` 범위는 `FILE_NAMES_INFORMATION`에 쓰기 및
구성 요소 간에 공유되는 `normalization_context` 슬롯을 제공합니다. 이 모든 것
객체는 소유하지 않은 콜백 기간 뷰입니다.

## 콜백 파사드

`ntl::flt::callback_data<Operation>`은 `FLT_CALLBACK_DATA`를 소유하지 않는
wrapper입니다. 모든 operation에는 `create_callback_data`, `write_callback_data`,
`cleanup_callback_data` 같은 공개 `<operation>_callback_data` alias가 있으므로
operation 자체가 callback signature의 일부가 됩니다. facade는 I/O status,
completion, operation-typed parameter 및 RAII 파일 이름
쿼리.
`ntl::flt::related_objects`는 필터, 볼륨,
인스턴스 및 커널 `FILE_OBJECT`.

낮은 수준의 네임스페이스 가상화의 경우
`create_parameters::try_replace_target_name()`는 대체품을
`FILE_OBJECT`~`IoReplaceFileObjectName`를 대상으로 합니다.
`clear_related_target()`는 전체 절대 교체가 발생하지 않도록 방지합니다.
이전 관련 파일 개체와 결합됩니다. 대부분의 시뮬레이션 재분석 필터
작업과 작업을 모두 수행하는 아래의 상위 수준 도우미를 사용해야 합니다.
필요한 완료 상태를 원자적으로 설정합니다.

### 시뮬레이션된 재분석 및 대상 복구

`try_complete_reparse()`는 단계 유형의 사전 생성 작업만 허용합니다. 패스
볼륨/장치 접두사를 포함하여 완전히 정규화된 교체
성공하면 `pre_result::complete`를 반환합니다.

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

도우미에는 `PASSIVE_LEVEL`에서 IRP 사전 생성이 필요합니다. 파일을 대체합니다.
객체 이름, 절대 대체를 위해 `RelatedFileObject`를 지웁니다.
그런 다음 `STATUS_REPARSE` 및 `IO_REPARSE`를 사용하여 요청을 완료합니다.

`IRP_MJ_NETWORK_QUERY_OPEN`는 일반적으로 빠른 I/O이며 시뮬레이션된 I/O를 반환할 수 없습니다.
재분석. 형식화된 매개변수 보기는 기본 생성 옵션을 노출하고
스택 플래그. 쿼리된 이름이 매핑에 속하는 경우 반환
`IoStatus`를 변경하지 않고 `pre_result::disallow_fast_io`; 필터 관리자
일반 재분석 도우미가 실행되는 느린 생성 경로를 발행합니다.

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

이름 바꾸기 및 하드 링크 요청에는 여러 관련 기본 버퍼 레이아웃이 포함됩니다.
`set_information_parameters::destination()`는 길이를 검증하고
`kind()`, `information_class()`를 사용하여 하나의 읽기 전용 보기를 반환합니다.
`root_directory()`, `name()`, `flags()`, `extended()` 및
`replace_if_exists()`. 이름 바꾸기/링크, 확장 및
기본 입력을 노출하지 않고 우회 액세스 확인 정보 클래스
버퍼:

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

`try_reissue_destination()`는 정보 클래스를 유지하고 이름 바꾸기/링크
플래그, 제한된 임시 버퍼 구축, 아래 `FltSetInformationFile` 호출
현재 인스턴스를 삭제하고 버퍼를 해제한 후 원래 작업을 완료합니다.
반환된 상태로 `PASSIVE_LEVEL`에서 실행되어야 하며 교체는
동일한 볼륨에 남아 있어야 합니다.

이름 터널링은 사전/사후 평생 계약입니다. 정규화된 상태를 유지합니다.
형식화된 사전 생성 또는 사전 설정 정보에서 얻은 `name_information`
완료 상태인 경우 `try_get_tunneled_name(as_post(data), pre_name)`를 호출하세요.
대응하는 작업 후 콜백에서 호출하십시오. 보관한 객체와 반환된 객체는 모두
RAII `name_information` 소유자입니다. 성공적인 결과에는
파일 시스템에 터널링된 이름이 없으면 소유자가 비어 있습니다.

이러한 API는 SimRep의 생성, 네트워크 쿼리 열기, 대상 및 터널링을 다룹니다.
이름 메커니즘. 디렉터리 열거, 알림, 쿼리 등을 수행하지 않습니다.
또는 파일 시스템 제어 결과를 자동으로 네임스페이스 인식형으로 만들지는 않습니다.
[SimRep 런타임 테스트](../../test/flt/runtime/SIMREP-README.ko-KR.md)는 완전히
격리된 드라이버/앱 쌍과 VM 검증 항목을 제공합니다. 여기에는 터널 이름 없이
성공하는 사례와 8.3 별칭을 보존된 긴 이름으로 복원하는 사례가 모두 포함됩니다.

[NameChanger 런타임 픽스처](../../test/flt/runtime/NAME-CHANGER-README.ko-KR.md)는
네임스페이스 이식에 필요한 추가 디렉터리 제어 작업을 보여줍니다.
콜백은 등록 경계에 형식화된 상태로 유지되지만 검증자는
`NtQueryDirectoryFile`를 직접 사용하여 10개의 디렉터리 레이아웃을 요청하고
대상 파일 시스템/OS가 허용하는 모든 레이아웃을 실행합니다. 또한 패턴, 연속
상태, 작은 버퍼, 단일 항목 반환, 다시 시작 스캔 및 레코드 체인 무결성도
검증합니다. 이는 픽스처에 명시적으로 열거된 레이아웃의 테스트 범위일 뿐,
임의의 디렉터리 정보 클래스가 같은 바이너리 레이아웃을 공유한다는 보장은
아닙니다.

### 파일 생성 후 열기 취소

감염되었거나 허용되지 않은 파일이 이미 열려 있을 수 있습니다.
생성 후 콜백이 성공적으로 실행되면 스택이 낮아집니다. 사용
해당 열기를 실행 취소하고 원래 생성 결과를 바꾸는 단계 유형 도우미:

```cpp
void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects) noexcept {
  if (data.io_status().is_err() || data.io_status() == STATUS_REPARSE)
    return;

  if (scan_file(objects) == verdict::infected)
    (void)ntl::flt::try_cancel_file_open(ntl::flt::as_post(data));
}
```

`try_cancel_file_open()`는 `post_operation<operation_id::create>`만 허용합니다.
성공적인 비재분석 생성, 오류 상태인 `PASSIVE_LEVEL`가 필요합니다.
`STATUS_ACCESS_DENIED`와 같은 사용자 핸들이 있는 파일 객체
아직 생성되지 않았습니다. `FltCancelFileOpen()`를 호출하고 완료합니다.
정보가 전혀 없는 원본 콜백 데이터. 사전 생성 작업, 원시
콜백 데이터, 성공 완료 상태 또는 다음이 포함된 파일
`FO_HANDLE_CREATED`가 거부되었습니다.

도우미는 이 열기의 결과를 변경합니다. 지속성을 확립하지 않습니다.
추후 오픈을 위한 정책입니다. 동기화되는 동안 결정을 수행합니다.
생성 후 콜백은 여전히 작업을 소유합니다. 마이크로소프트의
[`FltCancelFileOpen`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcancelfileopen)
계약서는 동일한 시간 제한을 문서화합니다.

### 삭제 처리 및 정리 확인

삭제 요청은 파일이 삭제되었다는 증거가 아닙니다. 실패할 수도 있어
이후 요청으로 해제되거나, 다른 요청과 경쟁하거나, 마지막 핸들이 정리될 때까지
보류 상태로 남을 수 있습니다.
마지막 핸들이 청소되었습니다. NTL은 네이티브를 노출하지 않고 요청을 노출합니다.
콜백 버퍼:

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

`disposition_information_view`는 두 가지 모두를 검증합니다.
`FileDispositionInformation` 및 `FileDispositionInformationEx`는 다음을 복사합니다.
작은 값을 가지며 `delete_requested()`, `extended()`,
`state_kind()`, `flags()`, `posix_semantics()`,
`force_image_section_check()`, `on_close()` 및
`ignore_readonly_attribute()`. 의도적으로 네이티브 버퍼 접근자가 없습니다.
`FILE_DISPOSITION_ON_CLOSE`를 포함하는 확장 요청이
닫을 때 삭제 상태; 기타 레거시 또는 확장 요청은 일반 제어를 제어합니다.
처분을 삭제합니다. 콜백 생성을 통해 초기 상태를 직접 검사할 수 있습니다.
`data.parameters().delete_on_close()`를 사용합니다.

성공적인 설정 정보 포스트 콜백 후에만 추적된 상태를 업데이트합니다.
처리 작업이 중복되면 완료 순서가 안정적이지 않습니다.
최종 상태를 설명합니다. 스트림을 불확실하다고 표시하고 확인합니다.
마지막으로 관찰된 포스트 콜백을 신뢰할 수 있는 것으로 처리하는 대신 정리합니다.

`try_query_cleanup_deletion()`는 단계 유형의 사후 정리만 허용합니다.
작동하며 `PASSIVE_LEVEL`에서 실행되어야 합니다. 일반적인 사전 정리 콜백
스트림 컨텍스트를 유지하고 `pre_result::synchronize`를 반환합니다. 대응하는
작업 후 콜백은 다음을 쿼리할 수 있습니다.

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

도우미는 `STATUS_FILE_DELETED`를 다음에서 번역합니다.
`FltQueryInformationFile(FileStandardInformation)`를 `deleted`로,
성공한 쿼리를 `present`로 변환하며 다른 모든 실패는 그대로 보존합니다.
[삭제 런타임 픽스처](../../test/flt/runtime/DELETE-README.ko-KR.md)는 다음을 제공합니다.
삭제 취소, 생성 시 delete-on-close, 확장 플래그, 보류 중인 핸들, 결정적으로
재현되는 disposition 경쟁 상태, 대체 데이터 스트림과 전체 파일을 구분하는
검증을 포함한 완전한 드라이버/앱 예제입니다.

### 하위 스택 작동 상태

`callback_data<Operation>::try_request_operation_status()`가 입력되었습니다.
`FltRequestOperationStatusCallback`의 대응물입니다. 에서만 호출하세요.
IRP 기반 요청의 사전 작업 콜백. 반환된 값을 관찰합니다.
하위 스택의 `IoCallDriver`가 반환될 때; 이는 대체품이 아닙니다.
작업의 마지막 작업 후 `IoStatus`.대부분의 필터에는 필요하지 않기 때문에 시설은 의도적으로 좁습니다. 그
일반적인 경우는 oplock FSCTL 및 디렉터리 변경 알림입니다.
`STATUS_PENDING`는 하위 스택이 비동기 요청을 수락했다고 말합니다.

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

콜백은 작업 유형의 읽기 전용 매개변수 스냅샷을 수신합니다.
원시 `PFLT_IO_PARAMETER_BLOCK`보다. 필터 관리자는 해당 스냅샷을 다음과 같이 캡처합니다.
요청 호출이므로 나중에 매개변수 변경사항이 반영되지 않습니다.

상태 개체는 비페이징 풀에 생성되고 NTL은 이를 정확하게 파괴합니다.
한 번: 요청이 실패한 경우 즉시 또는 상태 콜백이 반환된 후.
따라서 생성자와 소멸자는 `noexcept`여야 하고, 소유한 리소스는
`IRQL <= APC_LEVEL`에서 안전하게 해제할 수 있어야 합니다. 상태를 저장하는
오버로드는 풀 할당을 한 번 수행합니다. 요청 상태가 필요하지 않은 경우
`try_request_operation_status<&callback>()`는 컴파일 타임 콜백을 사용하고
할당하지 않습니다. 작업 상태를 묻지 않는 일반 콜백
또한 할당 비용도 지불하지 않습니다. 기본 제한 사항은 여전히 적용됩니다. 통화는 다음과 같습니다.
외부 사전 작업 처리 및 `IRP_MJ_CLOSE`에 대해서는 유효하지 않습니다.

### 자체 발행 필터 관리자 I/O

`try_allocate_callback_data()`는 실행된 I/O에 대한 콜백 데이터를 생성합니다.
minifilter 자체를 실행하고 이동 전용 `callback_data_owner`를 반환합니다. 선택
`prepare()`를 사용하여 작업을 생성하고 형식화된 매개변수만 구성합니다.
동기 또는 비동기 실행을 선택하기 전에 다음을 확인하세요.

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

비동기 제출에는 컴파일 시간 완료 콜백이 필요합니다. 는
완료는 소유권을 직접 받고 `try_perform_asynchronously()`를 받습니다.
복사 가능한 작업 핸들을 반환합니다.

```cpp
void complete_io(ntl::flt::callback_data_owner data) noexcept {}

auto operation = request->try_perform_asynchronously<complete_io>();
if (!operation)
  return operation.status();

// May race safely with inline or concurrent completion.
(void)operation->cancel();
return operation->wait();
```

원주민
[`FltPerformAsynchronousIo`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltperformasynchronousio)
계약은 제출 시 오류가 반환되는 경우에도 완료를 호출합니다. 는
따라서 원래 소유자는 제출 직전에 비어 있게 됩니다. 는
작업 핸들은 완료 시 `FltCancelIo`를 조정하므로 콜백 데이터
동시 취소에서 계속 사용하는 동안에는 해제할 수 없습니다. `wait()` 또한
명시적인 분해 경계를 제공합니다.

오버로드는 차용된 형식의 컨텍스트를 허용합니다.

```cpp
void complete_io(ntl::flt::callback_data_owner data,
                 io_state* state) noexcept;

io_state* nonpaged_state = acquire_long_lived_io_state();
auto operation =
    request->try_perform_asynchronously<complete_io>(nonpaged_state);
```

완료는 `IRQL <= DISPATCH_LEVEL`에서 실행할 수 있습니다. 컨텍스트 및 액세스
메모리는 완료될 때까지 유효하고 페이징되지 않은 상태로 유지되어야 합니다. 호출하지 마세요
에서 반환한 콜백 데이터에 대한 `FltSetCancelCompletion`
`FltAllocateCallbackData`: 해당 루틴은 기존 수신을 위한 것입니다.
생성된 작업이 아닌 작업 대기열에 게시되는 IRP 기반 작업
지원 IRP가 아직 구축되지 않았습니다.

### 일반 람다 및 편집기 완성

C++ 컴파일러는 일반 콜백을 허용하고 해당 콜백의 `auto`를 인스턴스화합니다.
작업 태그의 매개변수:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](auto data, auto objects, auto& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

`test/flt/compile/operation_callback.cpp`는 이 양식을 컴파일 상태로 유지합니다.
적용 범위. 이는 애플리케이션 코드에 권장되는 철자가 아닙니다.
현재 Visual Studio IntelliSense, Microsoft C/C++ VS Code 확장,
clangd는 상황별 유형을 추론하거나 진단할 수 있지만 여전히 no를 반환할 수 있습니다.
일반 람다 내부의 `data.`에 있는 멤버 목록입니다. 이것은 편집기/언어입니다
NTL 콜백 타이핑 또는 C++의 실패가 아닌 서버 완료 제한
편집.

따라서 공개 샘플에는 작업별 별칭이 명시되어 있으므로 멤버
완성, 탐색 및 매개변수 검색은 계속 사용할 수 있습니다.

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

멤버를 선택하는 대신 작업별 매개변수 보기를 사용하세요.
네이티브 `FLT_PARAMETERS` 유니온 수동:

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

`registration::on()`에 전달한 operation이 callback-data C++ 형식을 결정합니다.
create 등록은 `create_callback_data`를 받으며, 이 형식의 `parameters()`는
`create_parameters`만 반환합니다. write 등록은 `write_callback_data`를 받고 해당
`parameters()`는 `write_parameters`만 반환합니다. operation과 callback-data가
일치하지 않으면 `on()` overload를 해석하는 단계에서 거부됩니다.
과부하가 발생하므로 IDE 의미 분석을 통해 등록 시 오류를 보고할 수 있습니다.
콜백 본문이나 런타임 등록을 기다리는 대신 호출하세요.
일반 철자 `callback_data<operation::close>`는 다음과 같은 경우에 계속 사용할 수 있습니다.
코드 생성 또는 일반 등록 논리에 필요합니다.
코드는 읽기 또는 쓰기 매개변수에 대한 콜백 데이터 생성을 요청할 수 없습니다. 매개변수
세터는 `FltSetCallbackDataDirty()`를 자동으로 호출합니다. `native_iopb()`가 남아 있습니다.
특수 필터 관리자의 명시적인 탈출구로만 사용 가능
운영.

`name_information`는 다음에 의해 반환된 참조를 소유합니다.
`FltGetFileNameInformation`. 구문 분석된 경로를 읽기 전에 `try_parse()`를 호출하세요.
구성 요소. 파괴 시 `FltReleaseFileNameInformation`가 호출됩니다. 호출하지 마세요
`driver.start()`가 성공한 후에는 직접 `FltUnregisterFilter`를 수행하세요. NTL 항목
레이어는 등록 취소 및 crtsys 런타임 해체를 소유합니다.

## 일반 전용 포스트 콜백 및 완료 상태

`void* CompletionContext`가 없는 포스트 콜백 및
`post_operation_flags`는 정상적인 I/O 완료 작업이 포함되어 있음을 선언합니다.

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

NTL은 이 콜백 후에 `FLT_POSTOP_FINISHED_PROCESSING`를 반환합니다. 그것은 건너뛴다
필터 관리자가 `FLTFL_POST_OPERATION_DRAINING`를 제공할 때 콜백합니다. 이
형태는 사전 작업이 반드시 필요한 것을 획득하지 못한 경우에 적합합니다.
수술 후 출시됩니다.

사전 작업이 I/O별 상태를 소유해야 하는 경우 `on_with_completion<T>()`를 사용하세요.
수술 후:

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

`completion_slot<T>`는 `NonPagedPoolNx`에서 `T`를 할당합니다. 개체가 전송됩니다.
사전 작업이 반환된 경우에만 필터 관리자에
`success_with_callback` 또는 `synchronize`; 다른 모든 사전 작업 결과는 원인
즉각적인 파괴. `completion_ref<T>`는 다음에만 유효한 비소유 뷰입니다.
현재 포스트 콜백.

NTL은 모든 경로에서 객체를 소유하고 파괴합니다.

| 경로 | 사용자 작업 후 콜백 | 파괴 시점 |
| --- | --- | --- |
| 작업 전 콜백이 작업 후 콜백을 요청하지 않음 | 호출 안 함 | 작업 전 콜백 반환 전 |
| 정상 완료 | 호출 | 작업 후 콜백 직후 |
| 정상 완료에서 WhenSafe 요청 | 즉시 콜백과 안전 콜백 모두 호출 | 안전 콜백 직후 |
| WhenSafe를 예약할 수 없음 | 즉시 콜백만 호출 | NTL이 완료 처리를 재개하기 전 |
| 인스턴스 종료 처리 중, 플래그 없는 형식화된 작업 후 콜백 | 건너뜀 | 종료 처리 트램펄린에서 |
| 인스턴스 종료 처리 중, 플래그 인식 형식화된 작업 후 콜백 | `flags.draining()`과 함께 호출 | 콜백 직후 |

완료 상태 소멸자는 풀 메모리, 컨텍스트 참조, 객체 참조, 런다운 보호 및 미완료
I/O 카운터처럼 해당 I/O만 소유하는 리소스를 해제하기에 적합합니다. 다만 일반
완료 로직을 대신하지는 않습니다. 소멸자에서 최종 I/O 데이터를 검사하거나 성공
통계를 갱신하거나 새 작업을 시작하면 안 됩니다.

`T`는 `try_emplace()`에 전달한 인수로 예외 없이 생성할 수 있고 예외 없이
파괴할 수 있어야 합니다. 소멸자와 모든 RAII 멤버는 종료 처리 중을 포함해 작업
후 콜백이 실행될 수 있는 IRQL에서 적법해야 합니다. 상태는 비페이지이며 블로킹하지
않아야 합니다. 드라이버가 종료 처리를 명시적으로 관찰하거나, 네이티브 작업 후
결과를 반환하거나, 완료를 보류하거나, 형식화된 상태로 표현할 수 없는 소유권을
조정해야 한다면 플래그 인식 저수준 콜백을 사용하십시오.

## 게시 및 안전한 경우 처리

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

형식화된 완료 상태는 동일한 경로를 따를 수 있습니다.

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
관리자가 안전 콜백을 호출 또는 큐에 넣을 수 없을 때 이를 파괴합니다.

안전 콜백은 일반 `callback_data`가 아니라 `safe_post_operation<Operation>`을
받습니다. 일반적인 형식화된 상태와 매개변수에 접근하려면 `operation.data()`를
호출하십시오. WhenSafe IRQL 계약이 필요한 API는 이 래퍼만 받으므로 네이티브
작업 후 콜백에서 실수로 호출할 수 없습니다.

드라이버가 `FLT_POST_OPERATION_FLAGS`를 확인하거나 네이티브
`void* CompletionContext`를 관리하거나 네이티브 작업 후 결과를 반환해야 할 때는
플래그 인식 저수준 오버로드를 사용할 수 있습니다. 이 형식에서는 드라이버가 종료
처리를 직접 다뤄야 합니다. `flags.draining()`이 true이면 완료 상태만 해제하고
정상 완료 작업은 건너뛰며 WhenSafe를 요청해서는 안 됩니다.

읽기 및 쓰기 등록을 위해 안전한 콜백 NTL이 자동으로 설정됩니다.
`skip_paging_io`, 페이징 I/O를 이 방식으로 게시할 수 없기 때문입니다. 때
즉각적인 콜백은 IRP가 아닌 작업 또는 필터 관리자를 위해 WhenSafe를 요청합니다.
작업을 대기열에 넣을 수 없으며 안전한 콜백이 호출되지 않고 NTL이 작업을 완료합니다.
운영. 안전한 콜백은 `void`를 반환하므로 실수로 보류되는 일이 없습니다.
다시 완성. 따라서 필수 작업은 즉시 완료되어야 합니다.
이미 합법적일 때마다 콜백합니다. 업무에 안전한 콜백을 사용하세요
필터 관리자가 안전한 연속을 제공할 수 없는 경우 생략하는 것이 유효합니다.

## 형식화된 파일 시스템 컨텍스트

각 상태 유형을 한 번 선언하고 선언을 등록한 후 동일하게 사용합니다.
콜백에서 상태를 검색하거나 생성하기 위한 선언:

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
| `volume_context<T>` | 한 권 |
| `instance_context<T>` | 볼륨에 연결된 하나의 미니필터 인스턴스 |
| `file_context<T>` | 스트림 전체에 공유되는 인스턴스 내 하나의 파일 |
| `stream_context<T>` | 인스턴스 내의 하나의 파일 스트림 |
| `stream_handle_context<T>` | 스트림에 대해 하나의 열린 `FILE_OBJECT` |
| `transaction_context<T>` | 인스턴스 내의 하나의 파일 시스템 트랜잭션 |
| `section_context<T>` | 하나의 필터 관리자 데이터 스캔 섹션(Windows 8+) |

`try_get<T>()`는 `objects.try_get(declaration)`로 노출됩니다.
`try_get_or_create()`는 완전한 `FltGet*Context`를 수행합니다. /
`FltAllocateContext` / `FltSet*Context(KEEP_IF_EXISTS)` 시퀀스. 둘 때
스레드가 동일한 개체를 초기화하기 위해 경쟁하면 승자의 참조를 반환합니다.
컨텍스트를 파악하고 손실된 할당을 파괴합니다. `context_ref<T>`는 이동 전용이며
`FltReleaseContext`를 자동으로 호출합니다. 필터 관리자는 등록된
최종 참조가 사라질 때 콜백을 정리합니다. 그 콜백은
C++ 컨텍스트 소멸자.

`objects.try_remove(declaration)`는 설치된 볼륨을 원자적으로 분리합니다.
인스턴스, 파일, 스트림, 스트림 핸들 또는 트랜잭션 컨텍스트를 반환하고참조를 제거했습니다. `context_ref`만 해제해도 컨텍스트가 분리되지 않습니다.

컨텍스트 생성자와 소멸자는 `noexcept`여야 하며 과도하게 정렬되거나
`MAXUSHORT`보다 큰 상태는 컴파일 타임에 거부됩니다. `volume_context`
기본값은 유일한 풀인 `context_pool::nonpaged`(`NonPagedPool`)입니다.
필터 관리자는 해당 범위를 허용합니다. 공개 선언에서는 풀을 허용하지 않습니다.
인수이므로 `PagedPool` 또는 `NonPagedPoolNx`를 선택하면 컴파일 타임 오류가 발생합니다.
해당 진단 풀 태그만 구성할 수 있습니다. 다른 모든 범위의 기본값은 다음과 같습니다.
`context_pool::nonpaged_nx`(`NonPagedPoolNx`)를 명시적으로 선택할 수 있습니다.
`nonpaged`, `nonpaged_nx` 또는 `paged`. NTL은 각 당 하나의 C++ 상태 선언을 허용합니다.
필터 관리자는 해당 유형의 최대 하나의 컨텍스트를 연결하기 때문에 컨텍스트 범위
각 필터 인스턴스/개체 관계에 대해.

### 트랜잭션 컨텍스트

일반 작업 콜백에서 트랜잭션 상태를 생성하거나 검색합니다.
그런 다음 마스크에서 선택한 알림에 이를 등록합니다.

```cpp
auto state = objects.try_get_or_create(transaction_state_context);
if (!state)
  return state.status();

auto status = objects.try_enlist(
    *state, ntl::flt::transaction_notifications::commit_finalize |
                ntl::flt::transaction_notifications::rollback);
```

등록된 알림 루틴
`registration::on_transaction_notification(transaction_state_context, ...)`
획득이나 해제 없이 확인되고 빌린 `context_view<T>`를 받습니다.
참조:

```cpp
ntl::status transaction_notification(
    ntl::flt::related_objects objects,
    ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  state->last_notification = static_cast<ULONG>(notifications);
  return STATUS_SUCCESS;
}
```

정책에서 요구하는 경우에만 `objects.try_remove(transaction_state_context)`를 사용하세요.
조기 분리. 반환된 `context_ref`는 제거된 참조를 소유합니다.

### 데이터 스캔 섹션

섹션 컨텍스트는 `FltCreateSectionForDataScan`에 전달하며
`FltSetXxxContext` 루틴으로 설치하지 않습니다. 커널 소유 섹션에는 다음 수명
순서를 사용하십시오.

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

1. `try_register_for_data_scan()`에 인스턴스를 등록합니다.
2. `section_context<T>`를 할당합니다.
3. `OBJ_KERNEL_HANDLE`를 사용하여 `OBJECT_ATTRIBUTES`를 초기화합니다.
4. `try_create_data_scan_section()`에 호출하세요.
5. 필요에 따라 단면을 매핑하고 스캔합니다.
6. `ZwClose()`를 사용하여 반환된 핸들을 닫습니다.
7. `ObDereferenceObject()`를 사용하여 반환된 섹션 개체를 참조 취소합니다.
8. `close_data_scan_section()`와의 필터 관리자 연결을 닫습니다.
9. 나머지 `context_ref`를 놓습니다.

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
있으므로, create 호출 전에 섹션 컨텍스트의 콜백 가시 상태를 초기화하십시오.

`OBJ_KERNEL_HANDLE` 없이 생성하면 현재 프로세스에 속한 핸들이 반환됩니다. 이
핸들을 사용자 모드에 전달하면 안전하게 닫을 수 있는 위치와 시점이 달라집니다.
따라서 `data_scan_section`은 자동 RAII 닫기 객체가 아니라 네이티브 출력
레코드입니다. 이 순서는 네이티브
[`FltCreateSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcreatesectionfordatascan)
그리고
[`FltCloseSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltclosesectionfordatascan)
계약.

### 스캐너 및 AvScan 수명 주기 구성

섹션, 트랜잭션, 통신, 스왑 버퍼, 보류 중인 I/O 및
생성 후 취소 API는 의도적으로 독립적입니다. 스캐너 정책
작업 수명에 따라 구성합니다.

1. 사전 생성에서 대상을 선택하고 동기화된 사후 콜백을 요청합니다.
2. 생성 후 성공 시 읽기 전용 데이터 스캔 섹션을 통해 스캔하고
   앱이 감염된 결과를 반환하면 `try_cancel_file_open()`를 사용하세요.
3. 쓰기 가능한 열기에 `stream_handle_context<T>`를 연결합니다.
4. 비페이징 사전 쓰기에서는 `PASSIVE_LEVEL`를 연기하고 격리된 페이지를 캡처합니다.
   `try_swap_io_buffers()`를 사용하여 해당 소유자를
   `pending_pre_operation_queue`.
5. 형식화된 드라이버-앱 요청을 보냅니다. 허용된 쓰기를 재개하여 대기열
   상주 페이지를 설치하거나 감염된 쓰기를 취소합니다.
   `STATUS_ACCESS_DENIED`.
6. 사전 정리에서 최종 파일 내용을 다시 검사합니다. 정리는 실패할 수 없습니다
   매핑된 경로나 페이징 경로를 통해 이미 기록된 데이터를 실행 취소하므로 기록하거나제품 정책에 따라 결과를 수정하세요.
7. 트랜잭션된 파일의 경우 `transaction_context<T>`를 생성/등록하고 처리합니다.
   트랜잭션 소유권을 혼합하지 않고 커밋 마무리 및 롤백
   와이어 형식.

`swapped_io_buffers` 캡처를 삭제한 다음 보류된 쓰기를 재개하지 마십시오.
임의의 작업자의 원래 사용자 포인터를 사용합니다. 격리된 상태로 유지
보류 중인 대기열의 소유자입니다. 허용 경로는 상주 페이지를
더 낮은 스택 및 해당 거부/취소 경로는 오래된 스택을 노출하지 않고 스택을 해제합니다.
사용자 버퍼.

서비스 가용성은 별도의 정책 선택입니다. Microsoft의 스캐너 샘플
사용자 서비스를 사용할 수 없는 동안에는 페일오픈이 발생하므로 서비스와 시스템이
부트스트랩할 수 있습니다. 해당 정책을 채택하는 경우 매핑하기 전에 연결을 확인하고,
호출자 버퍼를 연기하거나 보류하고 전송 실패를 처리합니다.
요청이 이미 지속적으로 보류 중입니다.

완전한
[스캐너/AvScan 런타임 픽스처](../../test/flt/runtime/SCANNER-README.ko-KR.md)
x64 드라이버, x64 및 WOW64 앱, 열기/쓰기를 사용한 이 구성을 보여줍니다.
허용 및 거부 결정, 매핑된 쓰기 정리 감지, TxF
커밋/롤백, 섹션 충돌 방지 상태 및 균형 잡힌 소유권.

### 생성 시 캐시 이름

모든 읽기, 쓰기, 정리 또는 닫기 콜백에서 새 이름 쿼리를 만들지 마세요.
성공적인 생성 후 콜백은 `PASSIVE_LEVEL`에서 실행되므로 자연스러운 현상입니다.
최종 정규화된 이름을 한 번 쿼리하고 이를 형식화된 컨텍스트에 유지하는 위치:

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
파괴될 때까지 사용할 수 있습니다.

보관된 정보는 이름 바꾸기 구독이 아닌 스냅샷입니다. 필터
이름 바꾸기 또는 하드 링크를 추적하는 경우 해당 컨텍스트를 업데이트하거나 무효화해야 합니다.
해당 설정 정보 경로. 기본 필터 관리자 이름
정보는 페이징 풀에서 할당되므로 다음에서만 역참조하십시오.
`IRQL <= APC_LEVEL`; 이를 유지하면 쿼리는 방지되지만 페이징된 메모리는 생성되지 않습니다.
`DISPATCH_LEVEL`에서 합법적입니다.

컨텍스트 액세스는 필터 관리자의 기본 제한 사항을 따릅니다.

- 할당, 가져오기, 설정 및 지원 쿼리에는 `IRQL <= APC_LEVEL`가 필요합니다.
- 파일, 스트림 및 스트림 핸들 컨텍스트는 쿼리하거나 설정할 수 없습니다.
  생성 전 또는 종료 후 콜백
- 페이징 파일 및 일부 타사 파일 시스템은 이를 지원하지 않을 수 있습니다.
  상황; `supports()` 및 `try_get_or_create()`는 해당 결과를 유지합니다.
- `file_context`는 `FltSupportsFileContextsEx`를 사용하므로 필터 관리자의
  단일 스트림 파일 시스템 호환성 경로는 계속 사용 가능합니다.

Microsoft의 [미니필터 컨텍스트 관리](https://learn.microsoft.com/windows-hardware/drivers/ifs/managing-contexts-in-a-minifilter-driver)를 참조하세요.
및 [파일 시스템 컨텍스트 지원](https://learn.microsoft.com/windows-hardware/drivers/ifs/file-system-support-for-contexts)
기본 개체 및 파일 시스템 계약에 대한 것입니다.

## 형식화된 통신 포트

`ntl::flt::communication_server`는 동일한 `ntl::rpc::method`를 전달합니다.
필터 관리자 통신을 통해 IOCTL RPC 끝점에서 사용되는 설명자
항구. 설명자는 안정적인 메서드 ID, 인수 유형, 응답을 정의합니다.
형식, 직렬화된 요청 한도 및 디코드 할당 한도를 정의합니다. 전송 계층은 여전히
`FltCreateCommunicationPort`와 `FilterSendMessage`를 사용합니다.

드라이버와 앱이 공유하는 헤더에 계약을 한 번 선언합니다. 커널
확장은 콜백을 등록합니다. 사용자 모드 확장은 다음을 생성합니다.
`connect()`, `query_count()` 및 `query_count_async()`:

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

명시적인 클라이언트 인수는 의도적인 것입니다: 호출, 공유 영역 및
연결 상태는 모두 동일한 필터 관리자 연결에 유지됩니다.
생성된 각 함수에 대해 자동으로 다시 연결합니다.

### 비동기, 취소 및 코루틴

`FilterSendMessage` 자체는 동기식입니다. 따라서 NTL의 비동기 방법은 다음을 사용합니다.
제한된 요청을 제출하는 것뿐입니다. 미니필터는 다음에서 콜백을 실행합니다.
PASSIVE_LEVEL 작업자를 호출하고 `FltSendMessage`를 통해 결과를 보냅니다. 제한된
제한된 수의 중첩 `FilterGetMessage` 수신기는 고정 너비 요청 ID를 사용해 완료를
전달합니다. 하나의 클라이언트에서 여러 호출이 보류될 수 있지만
`communication_port_options::max_pending_async()` 경계는 보류 중인 작업 및
각 연결에 대해 독립적으로 요청 메모리를 복사했습니다(기본적으로 64개).

커널 비동기 콜백과 드라이버-앱 전달은 다음과 같이 대기열에 추가됩니다.
`queue_filter_work_item`, 원시 임원 작업 항목이 아닙니다. 필터 관리자가 거부함
삭제 후 새 작업이 시작되고 다음까지 필터 요약 참조를 유지합니다.
성공적으로 대기열에 추가된 각 콜백은 실제로 반환됩니다.

```cpp
auto first = product_messages::query_count_async(client, 40);
auto second = product_messages::query_count_async(client, 41);

const auto first_result = first.get();
const auto second_result = second.get();
```

`ready()`, `wait_for()`, `wait()`, `cancel()` 및 `get()`는 동일합니다.
`ntl::rpc::async_call`와 같은 단일 결과 소유권 모델. 취소는
협력: `cancel()`는 커널 요청과 장기 실행 콜백을 표시합니다.
`communication_context::cancellation_requested()`를 확인합니다. 완주하면 승리할 수 있다
완료가 취소보다 먼저 끝날 수 있으므로, 콜백은 취소 요청이 이미 반영된 작업까지
되돌린다고 가정해서는 안 됩니다.
요청은 이미 커밋된 작업을 취소합니다.

C++20에서는 `<ntl/flt/coroutine>`를 포함하고 생성된 비동기 호출을
`co_await`:

```cpp
task<std::uint32_t> query(ntl::flt::communication_client& client) {
  co_return co_await product_messages::query_count_async(client, 41);
}
```

생성된 `_async` 함수는 C++20에서 `std::stop_token`도 허용합니다. 는
토큰은 동일한 협력 취소를 요청합니다. C++14 이상에서는
일반적인 동기식 및 명시적 비동기 호출 API.

드라이버는 여러 개의 명명된 포트를 호스팅할 수 있습니다. 각 연결에는 독립적인
고정된 메모리 할당량 및 메서드 디스패치. NTL은 이전에 리스너를 닫습니다.
`FltUnregisterFilter`; 그런 다음 필터 관리자는 다음에 대한 연결 해제 콜백을 호출합니다.
서버 상태가 파괴되기 전에 남은 모든 클라이언트.

### 계약, 세션, 알림 및 스트림

앱과 미니필터가 유효성을 검사해야 하는 경우 `NTL_FLT_RPC_BEGIN_CONTRACT`를 사용하세요.
그들의 공유 계약. `connect()`는 엔드포인트를 쿼리하고 거부합니다.
다른 애플리케이션 버전 또는 첫 번째 이전에 누락된 기능 비트
메소드 호출.

계약 쿼리는 또한 모든 앱-드라이버 방법을 광고합니다.
고정 너비 요청을 통한 드라이버-앱 방법, 알림 및 스트림,
응답, 디코드 할당, 배치, 우선순위 제한 및 자동
파생된 와이어 스키마 지문. 정상적인 트래픽이 시작되기 전에 앱이 실패할 수 있음
공유 설명자가 실행 중인 드라이버와 일치하지 않는 경우:

```cpp
auto contract = client.query_contract();
client.require_method(product_messages::query_count_method);
client.require_client_method(product_messages::app_transform_method);
client.require_notification(product_messages::progress_notification);
client.require_stream(product_messages::numbers_stream);
```

명시적인 애플리케이션 계약 버전은 의미 체계 API 또는 수명 주기를 식별합니다.
변화. NTL은 직렬화된 필드 유형에서 각 구성원 지문을 파생합니다.
형식화된 멤버가 사용될 때 이를 확인합니다. 사용자 정의 유형은
C++14를 포함한 기존 `static serialize(Archive&, Self&)` 필드 목록.
컴파일러 형식 이름은 x86, x64 또는 MSVC 도구 집합 간에 안정적이지 않으므로
지문에서 제외됩니다.

### 연결 및 드라이버-앱 요청

`communication_server::on_connect()`는 클라이언트가 종료되기 전에 클라이언트를 수락하거나 거부할 수 있습니다.
포트를 사용할 수 있게 되고 형식화된 애플리케이션 상태를 첨부할 수 있습니다. `on_disconnect()`
커밋된 연결 해제를 관찰합니다. 복사된 `communication_connection`는 그대로 유지됩니다.
연결 해제 후 검사해도 안전함: `connected()`가 false가 되어 대상이 됨
이전 필터 관리자 포트를 역참조하는 대신 작업이 실패합니다.

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

동일한 포트는 양방향입니다. 드라이버 시작 메서드를 등록하기 전에
서버가 시작되고, 앱에 핸들러를 등록하고,
현재 콜백과 관련된 연결:

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

앱은 4개의 중첩된 `FilterGetMessage` 수신을 보류 상태로 유지합니다. 하나는 수신
경로는 비동기 메서드 완료, 알림, 스트림을 전달합니다.
기록 및 드라이버가 시작한 요청. 따라서 앱 요청 핸들러는 다음을 수행할 수 있습니다.
동시에 실행되며 이에 따라 공유 상태를 보호해야 합니다.

모든 연결은 하나의 서버 세션을 엽니다. 임시 알림은 다음과 같습니다.
최선을 다하고 ACK 없이 소비됩니다.

```cpp
auto wait = product_messages::progress_async(client);
// The driver calls publisher.try_notify(progress_notification, event).
auto event = wait.get();
```

`publisher.try_notify(notification, payload)`는 현재로 방송합니다.
가입자. `publisher.try_notify(connection, notification, payload)` 표적
연결된 가입자 1명. 다음 경우에 대상 배달이 `STATUS_NOT_FOUND`를 반환합니다.
연결이 끊어졌거나 해당 채널을 구독하지 않았습니다. 그것
조용히 방송이 되지는 않습니다.

신뢰할 수 있는 알림은 앱이 종료될 때까지 세션의 제한된 대기열에 남아 있습니다.
그 순서를 인정합니다. `max_reliable_records()` 및
`max_reliable_bytes()`는 각 세션에 독립적으로 적용되므로 앱이 정지됩니다.
제한 없이 커널 메모리를 늘릴 수 없습니다.

```cpp
auto delivery = product_messages::progress_reliable(client);
process(delivery.payload());
client.acknowledge(product_messages::progress_notification, delivery);
```

정상적인 핸들 또는 프로세스 종료는 해당 세션을 제거합니다. 없이 다시 연결하려면
승인되지 않은 신뢰할 수 있는 레코드가 손실되면 먼저 명시적으로 분리하세요. 이
현재 클라이언트를 무효화하고 보류 중인 작업을 취소한 후
`resume()`에서 사용하는 토큰:

```cpp
auto token = client.preserve_session();
auto resumed = product_messages::resume(token);
auto replayed = product_messages::progress_reliable(resumed);
resumed.acknowledge(product_messages::progress_notification, replayed);
```

신뢰할 수 있는 대기열은 외부가 아닌 이상 미니 필터가 로드된 상태로 유지되는 동안에만 활성화됩니다.
저장소가 명시적으로 설치되었습니다. 맞춤형 상점은 다음에서 파생될 수 있습니다.
`communication_notification_store`. NTL은 또한 선택 사항을 제공합니다.
`registry_notification_store`:

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

다시 연결 가능한 세션당 하나의 제한된 `REG_BINARY` 값을 유지합니다. NTL
레코드가 표시되기 전에 `persist()`를 호출하고 명시적인 레코드 후에 제거합니다.
ACK를 실행하고 메모리 내 세션 토큰이 없을 때 `restore()`를 호출합니다. 아니요
저장소가 설치되지 않으면 저장소 I/O가 발생합니다.

스토리지 후크는 NTL 연결이나 세션 잠금 없이 `PASSIVE_LEVEL`에서 실행됩니다.
스레드로부터 안전해야 합니다. `communication_record_view::data` 범위가 유효합니다.
후크 호출에만 해당됩니다. 일괄 배송에 사용되는 매장은 재정의되어야 합니다.
원자 연산으로 `acknowledge_batch()`; NTL은 의도적으로 에뮬레이트하지 않습니다.
외부에 표시되는 단일 레코드 커밋을 여러 개 만들어 일괄 처리합니다. 명시적으로
세션을 닫으면 `erase_session()`가 호출됩니다.

신뢰할 수 있는 수신은 제한된 배치를 요청할 수 있습니다.

```cpp
auto wait = client.receive_reliable_batch_async(
    product_messages::progress_notification);
auto batch = wait.get();
consume(batch.values());
client.acknowledge(product_messages::progress_notification, batch);
```

`max_reliable_records()` 및 `max_reliable_bytes()`는 각 세션을 바인딩했습니다.
`reliable_overflow(reject_newest)`는 대기 중인 데이터를 보존하고 새 데이터를 거부합니다.
기록. `reliable_overflow(drop_oldest)`는 그렇지 않은 가장 오래된 레코드를 제거합니다.
배송중. 후자는 외부인 경우 의도적으로 사용할 수 없습니다.
NTL은 애플리케이션 스토어가 다음을 처리한다고 가정할 수 없기 때문에 스토어가 설치됩니다.
ACK로 퇴거.

형식화된 스트림은 하나의 제한된 앱-드라이버 업로드 방법을 하나의 방법과 결합합니다.
주문된 ACK 기반 드라이버-앱 채널. 업로드 일괄 처리는 제한된 내용을 직렬화합니다.
하나의 요청으로 벡터를 생성합니다. 모든 다운로드 기록은 확인될 때까지 유지됩니다.

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

드라이버 콜백은 `communication_stream_context<Stream>`를 수신합니다. 그
`try_write()`, `try_complete()` 및 `try_fail()` 메소드는 데이터 또는
동일한 제한된 신뢰할 수 있는 대기열에 터미널 레코드를 기록합니다. 교차채널 우선순위
다음에 전달될 대기 채널을 선택합니다. 하나의 스트림 내 레코드 유지
시퀀스 및 ACK 계약.

알림 대기, 안정적인 대기, 스트림 쓰기 및 스트림 읽기 노출
동기식 및 비동기식 형태. C++20에서는 비동기 값이 다음과 같을 수 있습니다.
`co_await`ed이며 `std::stop_token`에 바인딩될 수 있습니다. C++14 이상에서는
명시적인 대기/취소/가져오기 API.

### 권한 부여 및 리소스 제한

`communication_port_options` 경계 연결, 보류 중인 비동기 호출,
유지된 세션, 신뢰할 수 있는 레코드/바이트 및 고정된 영역. 맞춤 설정
기본 ACL이 아닌 경우 필터 관리자 포트 보안 설명자
제품 정책. 방법별 정책의 경우 `on_authorized()` 및
`NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID`는 요청 역직렬화 전에 실행됩니다.

```cpp
NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID(
    product_messages, 0xA20, reply_type(request_type), privileged_call,
    [](const ntl::flt::communication_context& context) noexcept {
      return authorize_process(context.requestor_process_id());
    },
    [](request_type request) { return handle(request); })
```

정책은 `NTSTATUS` 또는 `ntl::status`를 반환합니다. 캡처한 원본을 사용할 수 있습니다.
일반 Windows 커널 보안을 사용하는 요청자 프로세스 ID 및 `session_id()`
API. 인증은 직렬 변환기 할당 전에 발생하므로 거부된 호출자는
보호된 메서드가 공격자가 제어하는 컨테이너를 디코딩하도록 강제할 수 없습니다.

### 공유 영역 및 교차 비트성

크거나 자주 교환되는 고정 레이아웃 데이터는 연결 바인딩을 사용할 수 있습니다.
모든 레코드를 직렬화하는 대신 공유 영역. 앱이 할당하고
지역을 등록한 다음 `ntl::ipc::buffer_token` 값을 통해 보냅니다.
일반 유형의 방법:

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

토큰에는 고정 너비 영역 ID, 오프셋 및 길이 필드가 포함됩니다.
프로세스 포인터. 따라서 동일한 프로토콜은 다음에 연결된 x86 앱을 지원합니다.
x64 미니필터. 해상도는 연결 소유권, 생성, 경계를 확인합니다.
고정된 범위를 노출하기 전에 액세스하세요. 파괴하거나 `close()`
해당 토큰을 사용하는 모든 동기화 또는 비동기 호출 후에만 `registered_port_region`
완료했습니다.
등록을 취소하면 앱이 가상 할당을 해제하기 전에 MDL이 해제됩니다.

가변 크기 페이로드의 경우 `registered_port_region::make_buffer_pool()`는 다음을 생성합니다.
`ntl::ipc::shared_buffer_pool`. 이동 전용 임대는 하위 범위를 예약하고
파기 시 범위를 풀로 반환합니다. 드라이버는 여전히 수신하고
일반 `buffer_token`를 검증합니다. 등록된 항구 지역은 오래 지속되어야 합니다.
모든 임대. 런타임 픽스처는 할당, 릴리스, 재사용 및
미니필터 어댑터를 통해 합쳐집니다.

메시지 콜백은 `PASSIVE_LEVEL`에서 실행됩니다. NTL은 필터 관리자의 것을 복사합니다.
디코딩 전 구조적 예외 처리에서 정렬되지 않은 사용자 버퍼
메소드의 요청/할당 제한을 적용하고 잘못된 프레이밍을 거부합니다.
또는 정상적인 연결을 끊지 않고 오래된 지역 토큰을 사용할 수 있습니다. 런타임
픽스처는 형식화된 호출, 동시 비동기 완료 디스패치, 협력적
취소, 공유 링, 잘못된 형식 및 지나치게 큰 프레임, 유효하지 않은
공유 지역 범위/액세스/할당량 토큰, 오래된 토큰 거부 및 연결
거부된 요청 후 재사용. 고급 통신 테스트도 포함됩니다.
형식화된 연결 상태, 연결/연결 해제 관찰, 연결 시 거부,연결/세션 할당량, 드라이버-앱 요청, 대상 전송 구독
확인, 상세한 계약 불일치, 신뢰할 수 있는 레코드/바이트 할당량, 신뢰할 수 있음
일괄 ACK 및 중복 거부, 외부 복원 후크, 가장 오래된 삭제
오버플로, 스트림 실패 완료, C++20 코루틴 호출 및 동시
연결/호출/닫기 스트레스 테스트를 수행합니다. 이러한 테스트 전용 경로는 온보딩
예제에는 포함되지 않습니다.
샘플.

## IRQL 및 수명

콜백의 기본 필터 관리자 계약이 항상 승리합니다.

- 드라이버 입력, 등록, 인스턴스 설정, 언로드가 가능합니다.
  `PASSIVE_LEVEL` 경로.
- 일부 사전 작업 콜백은 `APC_LEVEL`에서 실행될 수 있습니다.
- 사후 작업 콜백은 `DISPATCH_LEVEL`에서 실행될 수 있습니다. 페이징 가능한 코드를 유지하고,
  차단 작업, 예외 및 일반 CRT/STL은 해당 경로에서 작동합니다.
- `try_query_name()`는 이름 쿼리를 합법적으로 만들지 않습니다. I/O에서만 호출하세요.
  `FltGetFileNameInformation`가 허용되는 경로입니다.
- 문서에 RAII가 명시적으로 명시되지 않는 한 정면은 소유하지 않습니다.
  콜백 후에 콜백 데이터나 관련 개체 보기를 유지하지 마십시오.

조립식 [미니필터 샘플 카탈로그](../../examples/minifilter)는
여섯 가지 온보딩 문제:

- [`basic`](../../examples/minifilter/basic) 커버 유형
  생성/읽기/쓰기/정리 콜백 및 캐시된 스트림 이름 컨텍스트
- [`control-device`](../../examples/minifilter/control-device)는 WDK를 매핑합니다.
  형식화된 장치/IOCTL 처리기 및 언로드 거부권에 대한 CDO 수명 주기
- [`communication`](../../examples/minifilter/communication)는 필터를 분리합니다.
  관리자 포트, 형식화된 RPC, 알림, 스트림 및 공유 링 동작;
- [`operation-log`](../../examples/minifilter/operation-log)는 MiniSpy를 매핑합니다.
  흐름을 형식화된 콜백, 제한된 큐 및 형식화된 사용자 모드 종료 처리로 매핑합니다.
- [`swap-buffers`](../../examples/minifilter/swap-buffers) 사전 쓰기 교체
  데모 변환을 위한 입력 및 사전 READ 출력; 그리고
- [`volume-metadata`](../../examples/minifilter/volume-metadata)는
  MetadataManager 수명 주기를 형식화된 볼륨 잠금, 스냅샷, PnP, 종료 및
  해체 처리로 매핑합니다.

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
