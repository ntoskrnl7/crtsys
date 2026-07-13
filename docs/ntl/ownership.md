# NTL Handle and Object Ownership

[Back to NTL docs](./README.md)

NTL uses the same `ntl::unique_handle` name for native handle ownership in both
build modes:

- in user-mode builds, `ntl::unique_handle` closes with `CloseHandle`
- in kernel builds, `ntl::unique_handle` closes with `ZwClose`
- `ntl::unique_object<Pointer>` owns an object-manager reference and releases
  it with `ObDereferenceObject`.

The name is shared because app-side and driver-side code both work with native
`HANDLE` values, but the close primitive is selected by the build environment.
Referenced object pointers remain separate because their lifetime is not handle
ownership.

Header: [`include/ntl/handle`](../../include/ntl/handle)

## User-Mode Handles

Use `ntl::unique_handle` in user-mode companion apps that open a driver device
with `CreateFileW`, call `DeviceIoControl`, or use other Win32 APIs returning
handles closed by `CloseHandle`.

```cpp
ntl::unique_handle device{
    CreateFileW(LR"(\\.\MyDevice)",
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr)};
if (!device) {
  return GetLastError();
}

// device closes automatically with CloseHandle.
```

API:

- `unique_handle()`
- `unique_handle(HANDLE handle)`
- move construction / move assignment
- `get()`
- `put()`
  - clears current ownership and returns `HANDLE*` for Win32 out-parameters
- `close()`
  - closes with `CloseHandle` and returns `true` on success
- `reset(handle = nullptr)`
  - closes the old handle, ignores close failure, and optionally adopts another
- `release()`
  - detaches ownership without closing
- `valid()` / `operator bool()`

## Kernel Handles

Use `ntl::unique_handle` for handles returned by Zw/Nt-style kernel
APIs when the handle must be closed with `ZwClose`.

```cpp
ntl::unique_handle event_handle;
auto status = ZwCreateEvent(event_handle.put(),
                            EVENT_MODIFY_STATE | SYNCHRONIZE,
                            nullptr,
                            NotificationEvent,
                            FALSE);
if (!NT_SUCCESS(status)) {
  return ntl::status{status};
}

// event_handle closes automatically with ZwClose.
```

API:

- `unique_handle()`
- `unique_handle(HANDLE handle)`
- move construction / move assignment
- `get()`
- `put()`
  - clears current ownership and returns `PHANDLE` for WDK out-parameters
- `close()`
  - closes with `ZwClose` and returns `ntl::status`
- `reset(handle = nullptr)`
  - closes the old handle, ignores close failure, and optionally adopts another
- `release()`
  - detaches ownership without closing
- `valid()` / `operator bool()`

Prefer handles created with `OBJ_KERNEL_HANDLE` for driver-owned kernel handles.
If a handle came from user mode, first decide whether the driver really owns
that handle. Many IOCTL paths should reference the object with
`ObReferenceObjectByHandle` and then use `ntl::unique_object<Pointer>` for the
object reference instead of closing the user-provided handle.

## Object References

Use `ntl::unique_object<Pointer>` for pointers returned by object-manager
reference APIs, for example `ObReferenceObjectByHandle`.

```cpp
auto event = ntl::try_reference_object_by_handle<PKEVENT>(
    event_handle.get(),
    EVENT_MODIFY_STATE,
    *ExEventObjectType);
if (!event) {
  return event.status();
}

KeSetEvent(event->get(), IO_NO_INCREMENT, FALSE);

// The referenced event object is released with ObDereferenceObject.
```

API:

- `unique_object<Pointer>()`
- `unique_object<Pointer>(Pointer object)`
- move construction / move assignment
- `get()`
- `reset(object = nullptr)`
  - releases the old object reference with `ObDereferenceObject`
- `release()`
  - detaches ownership without dereferencing
- `valid()` / `operator bool()`
- `operator->()` and `operator*()` for non-void pointed-to types

`Pointer` is the native WDK pointer type, such as `PFILE_OBJECT`, `PDEVICE_OBJECT`,
or `PKEVENT`.

## Factory Helper

`try_reference_object_by_handle<Pointer>(...)` wraps
`ObReferenceObjectByHandle` and returns `ntl::result<ntl::unique_object<Pointer>>`.

```cpp
auto object = ntl::try_reference_object_by_handle<PFILE_OBJECT>(
    file_handle.get(),
    FILE_READ_DATA,
    *IoFileObjectType);
if (!object) {
  return object.status();
}
```

## IRQL

Use these helpers from `PASSIVE_LEVEL` unless the exact WDK primitive documents
a wider contract. `ZwClose` and `ObReferenceObjectByHandle` are control-path
object-manager operations, not DPC/ISR helpers.

## Notes

Do not use `unique_object` for `HANDLE` values, and do not use `unique_handle`
for object-manager reference pointers. The wrappers intentionally keep those
ownership models separate.
