# NTL Registry Helper

[Back to NTL docs](./README.md)

Header: [`include/ntl/registry`](../../include/ntl/registry)

`ntl::registry_key` is an RAII wrapper for native Zw registry key handles. It is
intended for driver setup, unload, and configuration paths that need to read or
write `\Registry\...` keys while keeping `NTSTATUS` visible.

## Driver Parameters

`ntl::main` receives the service registry path from `DriverEntry`. The common
driver configuration key is the `Parameters` subkey below that path:

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

`try_open_driver_parameters(registry_path)` opens:

```text
<registry_path>\Parameters
```

The helper does not create the key. Use `registry_key::create` when a driver
needs to create its own volatile or persistent test/configuration key.

`open_driver_parameters` is kept as a compact alias. New examples use the
`try_` spelling to match other NTL helpers that return `ntl::result<T>`.

## Querying Values

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

Typed query helpers validate the `REG_*` type and return `ntl::result<T>`:

- `query_dword(name) -> ntl::result<std::uint32_t>`
- `query_qword(name) -> ntl::result<std::uint64_t>`
- `query_string(name) -> ntl::result<std::wstring>`
- `query_binary(name) -> ntl::result<std::vector<std::uint8_t>>`
- `query_value(name) -> ntl::result<ntl::registry_value>`

`query_string` accepts `REG_SZ` and `REG_EXPAND_SZ` and trims trailing NUL
characters. It does not expand environment variables.

## Setting Values

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

Available setters:

- `set_value(name, type, data, bytes)`
- `set_dword(name, value)`
- `set_qword(name, value)`
- `set_string(name, value)`
- `set_expand_string(name, value)`
- `set_binary(name, bytes)`
- `delete_value(name)`
- `delete_key()`

## Ownership

`registry_key` owns a native key handle through
[`ntl::unique_kernel_handle`](./ownership.md). It supports move, `close()`,
`reset()`, `release()`, and `put()` for WDK out-parameters.

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

Treat `ntl::registry_key` as `PASSIVE_LEVEL` only. Zw registry calls are
control-path operations and the helper uses `std::wstring`, `std::vector`, and
`ntl::result<T>`.

## Driver Test Coverage

The driver test covers:

- volatile key create/open/delete
- `REG_DWORD`, `REG_QWORD`, `REG_SZ`, `REG_EXPAND_SZ`, and `REG_BINARY`
- raw `query_value`
- value deletion and missing-value status
- move, release, adopt, and close ownership paths
