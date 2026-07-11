# NTL RPC

[Back to NTL docs](./README.md)

NTL RPC generates matching kernel server and user-mode client stubs around
`DeviceIoControl`. Serialization uses `zpp::serializer`.

Headers:

- [`include/ntl/rpc/common`](../../include/ntl/rpc/common)
- [`include/ntl/rpc/server`](../../include/ntl/rpc/server)
- [`include/ntl/rpc/client`](../../include/ntl/rpc/client)

## Schema Macros

- `NTL_RPC_BEGIN(name)`
- `NTL_RPC_END(name)`
- `NTL_ADD_CALLBACK_0`
- `NTL_ADD_CALLBACK_1`
- `NTL_ADD_CALLBACK_2`
- `NTL_ADD_CALLBACK_3`
- `NTL_ADD_CALLBACK_4`
- `NTL_ADD_CALLBACK_5`
- `NTL_ADD_CALLBACK_ID_0`
- `NTL_ADD_CALLBACK_ID_1`
- `NTL_ADD_CALLBACK_ID_2`
- `NTL_ADD_CALLBACK_ID_3`
- `NTL_ADD_CALLBACK_ID_4`
- `NTL_ADD_CALLBACK_ID_5`

`NTL_ADD_CALLBACK_N` uses `__LINE__` as the callback ID. This is convenient for
small internal or test schemas, but the schema line number becomes part of the
ABI. Use `NTL_ADD_CALLBACK_ID_N` when callback IDs need to remain stable across
schema formatting or reordering.

## Shared Schema

```cpp
// shared/demo_rpc.hpp
#pragma once

// Include after <ntl/rpc/server> in the driver and after <ntl/rpc/client> in
// the user-mode app.

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_ID_2(demo_rpc, 0x801, int, add, int, a, int, b, {
  return a + b;
})

NTL_ADD_CALLBACK_ID_1(demo_rpc, 0x802, int, negate, int, value, {
  return -value;
})

NTL_RPC_END(demo_rpc)
```

For types with commas, such as `std::map<int, int>`, wrap the type with
`NTL_RPC_ARG_PACK(...)`. For custom objects, provide a `zpp::serializer`
serialization function. The tested `point` class in
[`test/cmake/common/rpc.hpp`](../../test/cmake/common/rpc.hpp) shows the
pattern.

## Kernel Server

```cpp
#include <memory>
#include <string>
#include <ntl/driver>
#include <ntl/rpc/server>
#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto rpc_server = demo_rpc::init(driver);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset();
  });

  return ntl::status::ok();
}
```

`demo_rpc::init(driver)` creates the RPC device. Keep the returned
`std::shared_ptr<ntl::rpc::server>` alive for as long as the endpoint should
exist. A common pattern is to capture it in the unload callback.

Server callbacks run in the driver's device-control dispatch path. Keep them
short, validate inputs, and use the same IRQL discipline as other runtime-backed
driver control code.

## User-Mode Client

```cpp
#include <exception>
#include <iostream>
#include <ntl/rpc/client>
#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    std::wcout << L"40 + 2 = " << demo_rpc::add(40, 2) << L"\n";

    ntl::rpc::client client(L"demo_rpc");
    auto value = client.invoke<int>(demo_rpc::negate_1_index, 7);
    std::wcout << L"negate(7) = " << value << L"\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "RPC failed: " << e.what() << "\n";
    return 1;
  }
}
```

The generated wrapper functions, such as `demo_rpc::add`, create a temporary
client and call the matching device. Use an explicit `ntl::rpc::client` when
you want to reuse the handle or control the output buffer size.

IRQL: server-side callbacks should be treated as `PASSIVE_LEVEL`. The user-mode
`ntl::rpc::client` side is outside kernel IRQL rules but still depends on the
driver-side dispatch contract.
