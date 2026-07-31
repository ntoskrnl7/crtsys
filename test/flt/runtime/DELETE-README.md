# NTL Delete Runtime Fixture

This isolated driver/app pair verifies the reusable mechanisms from
Microsoft's `delete` minifilter sample. It observes only files below
`\crtsys-flt-delete-runtime` and uses development altitude `370030.232`.

The fixture separates a delete request from a confirmed deletion:

- `create_parameters::delete_on_close()` recognizes
  `FILE_DELETE_ON_CLOSE`;
- `set_information_parameters::disposition()` validates and copies legacy
  `FILE_DISPOSITION_INFORMATION` or extended
  `FILE_DISPOSITION_INFORMATION_EX` into a read-only typed view;
- each stream context tracks successful disposition and delete-on-close state;
- overlapping disposition operations make that tracked state uncertain, so
  cleanup is still checked instead of trusting whichever post callback ran
  last;
- a synchronized post-cleanup callback calls
  `try_query_cleanup_deletion(as_post(data))`. `STATUS_FILE_DELETED` confirms
  deletion; a successful standard-information query confirms that the stream
  remains present.

`FILE_DISPOSITION_ON_CLOSE` selects the delete-on-close state controlled by
an extended disposition. The runtime case creates a handle with
`FILE_FLAG_DELETE_ON_CLOSE`, clears that state with an extended
`ON_CLOSE` request without `DELETE`, and proves that the file survives.
Trying to use `DELETE | ON_CLOSE` as a new delete request on an ordinary NTFS
handle is not equivalent and is rejected by NTFS.

## Assertions

The app proves all of the following in a loaded x64 minifilter:

1. Legacy delete disposition can be set and cleared before cleanup.
2. Extended `ON_CLOSE` can clear an existing create-time delete-on-close
   state, while an unchanged `FILE_DELETE_ON_CLOSE` request deletes its file.
3. Extended POSIX, force-image-section-check, and ignore-readonly flags are
   parsed, and a read-only file is deleted successfully.
4. Deleting an alternate data stream leaves the base file present and is
   reported as a stream deletion; deleting the base is reported as a whole
   file deletion.
5. An existing second handle remains usable while deletion is pending, and
   the file disappears after the last handle closes.
6. A deterministic two-thread gate overlaps set and clear disposition
   operations on one stream. The driver records the race and later confirms
   the final deletion at cleanup.
7. Typed completion-state ownership balances exactly, the filter unloads, and
   both x64 and x86 apps pass against the same x64 driver.

## Build

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64_v145
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_delete_runtime_test `
           crtsys_flt_delete_runtime_test_app

cmake -S test\flt\runtime -B test\flt\runtime\build_x86_v145
cmake --build test\flt\runtime\build_x86_v145 --config Debug `
  --target crtsys_flt_delete_runtime_test_app
```

Stage the driver package and application using the
[disposable VM workflow](README.md#disposable-vm-execution). Run the application
against an explicit disposable volume root:

```powershell
$testVolumeRoot = Read-Host 'Disposable test volume root'
.\crtsys_flt_delete_runtime_test_app.exe $testVolumeRoot
```

A passing app reports counters similar to:

```text
create_delete_on_close=2 legacy=8 extended=2 delete=7 clear=3
on_close=1 posix=1 force_image=1 ignore_readonly=1
set_success=10 set_failure=0 races=1 race_arrivals=2
cleanup_checks=8 cleanup_present=2 file_deletions=5 stream_deletions=1
completion_states=20/20
NTL delete runtime test PASS
```

The implementation is in
[`delete_driver/main.cpp`](delete_driver/main.cpp), the verifier is
[`delete_app/main.cpp`](delete_app/main.cpp), and their fixed-width RPC
contract is
[`delete_shared/delete_runtime.hpp`](delete_shared/delete_runtime.hpp).

Microsoft contracts used by the fixture:

- [FILE_DISPOSITION_INFORMATION_EX](https://learn.microsoft.com/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex)
- [FltQueryInformationFile](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltqueryinformationfile)
- [FILE_INFORMATION_CLASS](https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class)
- [FILE_DISPOSITION_INFO](https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-file_disposition_info)
