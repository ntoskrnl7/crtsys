# File Object Facades

[Back to NTL documentation](./README.md)

`ntl::file` is a non-owning C++ view of a kernel `PFILE_OBJECT`. It does not
open a path, close a `HANDLE`, take an object-manager reference, or replace
`std::fstream` and `std::filesystem`.

Use it when the I/O manager or a framework callback already supplies a live
`FILE_OBJECT` and the driver wants a typed view of common fields:

```cpp
void inspect(PFILE_OBJECT native_file) noexcept {
  ntl::file file{native_file};

  const std::wstring_view name = file.name();
  const bool readable = file.can_read();
  const bool writable = file.can_write();
  PDEVICE_OBJECT target = file.device_object();
}
```

The view remains valid only while the caller's native `FILE_OBJECT` contract
does. When code explicitly owns an object-manager reference, store it in
`ntl::unique_object<PFILE_OBJECT>` and construct a temporary `ntl::file` from
`owner.get()` while inspecting it.

## KMDF Bridge

`ntl::kmdf::file` observes a `WDFFILEOBJECT`. It exposes WDF-level file name,
device, and C++ context access. `wdm()` returns the corresponding `ntl::file`:

```cpp
constexpr auto on_file_create =
    +[](ntl::kmdf::device, ntl::kmdf::request request,
        ntl::kmdf::file file) noexcept {
      const ntl::file native_file = file.wdm();
      DbgPrint("read=%u write=%u\n", native_file.can_read(),
               native_file.can_write());
      request.complete(STATUS_SUCCESS);
    };

ntl::kmdf::file_config<> files;
files.on_create<on_file_create>();
```

Neither facade owns the WDF or WDM object. `native_object()` is available for
native API interoperation.

## Choose The Right Type

| Need | Type |
| --- | --- |
| Observe a callback-provided `PFILE_OBJECT` | `ntl::file` |
| Observe a callback-provided `WDFFILEOBJECT` | `ntl::kmdf::file` |
| Own an object-manager reference | `ntl::unique_object<PFILE_OBJECT>` |
| Own a `ZwCreateFile` handle | `ntl::unique_handle` |
| Perform standard C++ stream/filesystem operations | `std::fstream`, `std::filesystem` |
