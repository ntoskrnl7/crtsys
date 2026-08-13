# NTL 레지스트리 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/registry`](../../include/ntl/registry)

`ntl::registry_key`는 네이티브 Zw 레지스트리 키 핸들용 RAII 래퍼입니다. `NTSTATUS`를
그대로 보존하면서 `\Registry\...` 키를 읽거나 써야 하는 드라이버 설정·언로드·구성
경로를 위한 도구입니다.

## 드라이버 매개 변수

`ntl::main`은 `DriverEntry`에서 서비스 레지스트리 경로를 받습니다. 일반적인
드라이버 구성 키는 이 경로 아래의 `Parameters` 하위 키입니다.

```cpp
#include <ntl/registry>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  auto parameters = ntl::try_open_driver_parameters(registry_path);
  if (!parameters) {
    // Missing Parameters is often acceptable; decide per driver.
    if (static_cast<NTSTATUS>(parameters.status()) ==
        STATUS_OBJECT_NAME_NOT_FOUND) {
      return ntl::status::ok();
    }
    return parameters.status();
  }

  auto flags = parameters->query_dword(L"Flags");
  if (flags) {
    configure_driver(*flags);
  }

  return ntl::status::ok();
}
```

`try_open_driver_parameters(registry_path)`는 다음 키를 엽니다.

```text
<registry_path>\Parameters
```

이 도우미는 키를 만들지 않습니다. 드라이버가 자체 휘발성 또는 영구 테스트/구성 키를
만들어야 하면 `registry_key::create`를 사용하세요.

`ntl::driver_config`는 같은 `Parameters` 키를 위한 작은 편의 래퍼입니다. `key()`로
기본 `registry_key`를 노출하고, 선택 설정에 기본값을 반환하는 도우미를 추가합니다.

```cpp
auto config = ntl::driver_config::open(registry_path);
if (!config) {
  if (static_cast<NTSTATUS>(config.status()) ==
      STATUS_OBJECT_NAME_NOT_FOUND) {
    return ntl::status::ok();
  }
  return config.status();
}

const auto queue_depth = config->dword_or(L"QueueDepth", 32);
const auto mode = config->string_or(L"Mode", L"default");
```

테스트나 설정 코드에서 `Parameters` 키를 만들어야 하면
`driver_config::create(registry_path, ...)`를 사용하세요.

## 값 조회

```cpp
auto key = ntl::registry_key::open(
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\demo",
    KEY_READ);
if (!key) {
  return key.status();
}

auto name = key->query_string(L"DisplayName");
auto flags = key->query_dword(L"Flags");
auto blob = key->query_binary(L"OpaqueData");
```

타입이 지정된 조회 도우미는 `REG_*` 형식을 검증하고 `ntl::result<T>`를 반환합니다.

- `query_dword(name) -> ntl::result<std::uint32_t>`
- `query_qword(name) -> ntl::result<std::uint64_t>`
- `query_string(name) -> ntl::result<std::wstring>`
- `query_binary(name) -> ntl::result<std::vector<std::uint8_t>>`
- `query_value(name) -> ntl::result<ntl::registry_value>`

`driver_config`는 같은 타입이 지정된 조회 도우미를 전달하며 다음도 제공합니다.

- `dword_or(name, fallback)`
- `qword_or(name, fallback)`
- `string_or(name, fallback)`
- `binary_or(name, fallback)`

`query_string`은 `REG_SZ`와 `REG_EXPAND_SZ`를 받고 끝의 NUL 문자를 제거합니다.
환경 변수는 확장하지 않습니다.

## 값 설정

```cpp
auto key = ntl::registry_key::create(
    L"\\Registry\\Machine\\Software\\DemoDriver",
    KEY_READ | KEY_WRITE,
    REG_OPTION_VOLATILE);
if (!key) {
  return key.status();
}

key->set_dword(L"Flags", 0x10);
key->set_string(L"Name", L"demo");
key->set_binary(L"Seed", std::vector<std::uint8_t>{1, 2, 3, 4});
```

사용 가능한 설정 함수:

- `set_value(name, type, data, bytes)`
- `set_dword(name, value)`
- `set_qword(name, value)`
- `set_string(name, value)`
- `set_expand_string(name, value)`
- `set_binary(name, bytes)`
- `delete_value(name)`
- `delete_key()`

## 소유권

`registry_key`는 [`ntl::unique_handle`](./ownership.ko-KR.md)를 통해 네이티브
키 핸들을 소유합니다. 이동, `close()`, `reset()`, `release()`, WDK 출력 매개 변수용
`put()`을 지원합니다.

```cpp
auto opened = ntl::registry_key::open(path, KEY_READ);
if (!opened) {
  return opened.status();
}

ntl::registry_key key(std::move(*opened));
HANDLE raw = key.release();
ntl::registry_key adopted(raw);
```

## IRQL

`ntl::registry_key`는 `PASSIVE_LEVEL` 전용으로 취급하세요. Zw 레지스트리 호출은
제어 경로 작업이며 이 도우미는 `std::wstring`, `std::vector`, `ntl::result<T>`를
사용합니다.

## 드라이버 테스트 범위

드라이버 테스트는 다음을 다룹니다.

- 휘발성 키 생성/열기/삭제
- `REG_DWORD`, `REG_QWORD`, `REG_SZ`, `REG_EXPAND_SZ`, `REG_BINARY`
- 원시 `query_value`
- `driver_config`의 기본값 반환 조회
- 값 삭제와 없는 값 상태
- 이동, release, 인계, close 소유권 경로
