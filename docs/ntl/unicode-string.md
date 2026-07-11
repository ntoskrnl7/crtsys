# NTL Unicode String

[Back to NTL docs](./README.md)

Header: [`include/ntl/unicode_string`](../../include/ntl/unicode_string)

`ntl::unicode_string` adapts `std::wstring` storage to a `UNICODE_STRING`.
It is useful when driver setup code builds a name with C++ string utilities and
then calls a WDK API that expects `PUNICODE_STRING`.

API:

- construct from `std::wstring`
- `c_str() const`
- `operator*()`

Example:

```cpp
std::wstring name = L"\\Device\\demo";
ntl::unicode_string native_name{name};

OBJECT_ATTRIBUTES attributes;
InitializeObjectAttributes(&attributes,
                           &*native_name,
                           OBJ_KERNEL_HANDLE,
                           nullptr,
                           nullptr);
```

The `UNICODE_STRING` view borrows from the owning C++ string storage inside the
`ntl::unicode_string` object. Keep the object alive for as long as the native
string view is used.

IRQL: construction from `std::wstring` should be treated as `PASSIVE_LEVEL`.
Accessors are lightweight, but the lifetime and storage still belong to the
owning C++ object.
