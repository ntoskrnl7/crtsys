# NTL Handle and Object Ownership

[Back to NTL docs](./README.md)

NTL separates two WDK ownership models that are easy to confuse:

- `ntl::unique_kernel_handle` owns a native `HANDLE` and closes it with
  `ZwClose`.
- `ntl::unique_object<Pointer>` owns an object-manager reference and releases
  it with `ObDereferenceObject`.

Header: [`include/ntl/handle`](../../include/ntl/handle)

## Kernel Handles

Use `ntl::unique_kernel_handle` for handles returned by Zw/Nt-style kernel
APIs when the handle must be closed with `ZwClose`.

```cpp
ntl::unique_kernel_handle event_handle;
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

- `unique_kernel_handle()`
- `unique_kernel_handle(HANDLE handle)`
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

Do not use `unique_kernel_handle` for referenced object pointers, and do not use
`unique_object` for `HANDLE` values. The two wrappers intentionally keep those
ownership models separate.
