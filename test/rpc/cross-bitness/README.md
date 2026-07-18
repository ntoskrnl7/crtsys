# NTL RPC cross-bitness fixture

This fixture pairs an x64 NTL RPC driver with an x86 WOW64 client. It verifies
that RPC serialization is independent of native pointer width for the portable
schema surface.

Covered values include:

- fixed-width signed and unsigned integer boundary values, `float`, `double`,
  `bool`, and an enum with an explicit underlying type;
- `string` and `wstring`, including embedded null characters;
- empty and large `vector` values, nested vectors, `array`, `list`, `deque`,
  `set`, `multiset`, `unordered_set`, `map`, `multimap`, `unordered_map`,
  `pair`, and `tuple`;
- `optional`, `variant`, and nested user-defined serialized objects;
- a bounded-response mismatch that must fail before the server callback runs;
- a reply whose declared client type is wider than the returned payload;
- truncated input, oversized contiguous and nested-container counts, an
  over-limit request, and an unknown callback ID, followed by a valid request
  to prove the endpoint remains usable;
- the default secure endpoint denying a restricted impersonation token;
- concurrent clients calling one immutable dispatch table;
- server rundown rejecting new calls while allowing an active callback to
  finish before teardown;
- contract discovery from an x86 client, including exact version, transport
  features, application capabilities, and the sorted x64 server method list;
- deliberate version, capability, method, and transport-feature mismatches
  producing their corresponding compatibility diagnostics.

Native-width values such as `size_t`, `ptrdiff_t`, `uintptr_t`, `ULONG_PTR`,
and structures containing pointers are not portable RPC schema types. The
fixture carries size and difference values as `uint64_t` and `int64_t` instead.
The serializer writes fundamental values using their native `sizeof`, so a
native-width alias would encode four bytes in the x86 app and eight bytes in
the x64 driver.

Configure the two architectures separately. Build only the app target from the
Win32 tree and only the driver target from the x64 tree, then run them together
in the VM.

```powershell
cmake -S . -B build_x64 -A x64
cmake --build build_x64 --config Release `
  --target crtsys_rpc_cross_bitness_driver crtsys_rpc_cross_bitness_app

cmake -S . -B build_x86 -A Win32
cmake --build build_x86 --config Release `
  --target crtsys_rpc_cross_bitness_app
```

The expected x64-driver/x86-client result is:

```text
RPC cross-bitness PASS: client=x86 server=x64 boundary=1 empty=1 large_bytes=131072 large_numbers=32768 bounded_response=1 malformed=5 security=1 contract=4 concurrent=1024 rundown=1
```
