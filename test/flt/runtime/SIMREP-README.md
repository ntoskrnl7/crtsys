# NTL SimRep Runtime Fixture

This isolated driver/app pair verifies the reusable mechanisms from
Microsoft's SimRep minifilter sample. It maps the visible volume-relative path
`\crtsys-flt-simrep-visible` to
`\crtsys-flt-simrep-backing` at development altitude `370030.231`.

The driver uses only typed NTL callback boundaries:

- `try_complete_reparse(as_pre(create), absolute_name, absolute)` replaces the
  `FILE_OBJECT` name and completes with `STATUS_REPARSE`/`IO_REPARSE`;
- `network_query_open_parameters` identifies paging-file and open-by-ID
  requests, while a mapped Fast I/O request returns
  `pre_result::disallow_fast_io`;
- `set_information_parameters::destination()` validates rename/link layouts,
  `try_query_destination_name()` resolves the target, and
  `try_reissue_destination()` sends the repaired request below the instance;
- a typed create completion state retains normalized pre-operation
  `name_information`, and the post callback calls
  `try_get_tunneled_name()`.

The app creates an empty physical visible directory only so destination-name
resolution below this non-name-provider test filter can resolve the parent.
An explicit one-shot RPC bypasses redirection for exactly that root create or
remove. All child opens remain mapped.

## Assertions

The app proves all of the following in a loaded minifilter:

1. Reading and creating through the visible path reaches the backing path.
2. `NtQueryFullAttributesFile` reaches `IRP_MJ_NETWORK_QUERY_OPEN`, mapped
   Fast I/O is disallowed, and the reissued slow create is reparsed.
3. `MoveFileExW` and `CreateHardLinkW` destinations are resolved, rewritten,
   and reissued to the backing directory.
4. The app creates a long name with an 8.3 alias, deletes it through the short
   name, and recreates that short name. `FltGetTunneledName` must return a
   non-null owner whose parsed final component restores
   `Tunneled Long Name.tmp`; every typed completion state must also be
   destroyed exactly once.
5. The same x64 driver passes with x64 and x86 apps, unloads, and leaves no
   mapped test tree behind.

The fixture deliberately does not claim general namespace virtualization.
Directory enumeration, notification, name-provider callbacks, query
information, and FSCTL result rewriting belong to the broader NameChanger
surface.

## Build

```powershell
cmake -S test\flt\runtime -B test\flt\runtime\build_x64_v145
cmake --build test\flt\runtime\build_x64_v145 --config Debug `
  --target crtsys_flt_simrep_runtime_test `
           crtsys_flt_simrep_runtime_test_app

cmake -S test\flt\runtime -B test\flt\runtime\build_x86_v145
cmake --build test\flt\runtime\build_x86_v145 --config Debug `
  --target crtsys_flt_simrep_runtime_test_app
```

Run only through the disposable-VM command in
`D:\projects\crtsys-vm-test\COMMANDS.md`; never load the test driver on the
host. A passing app reports counters similar to:

```text
reparses=5 network_disallowed=1 destination_queries=2
renames_reissued=1 links_reissued=1 tunnel_successes=4
tunnel_names_found=1 tunnel_names_verified=1 tunnel_states=4/4
NTL SimRep runtime test PASS
```

The implementation is in
[`simrep_driver/main.cpp`](simrep_driver/main.cpp), the verifier is
[`simrep_app/main.cpp`](simrep_app/main.cpp), and their fixed-width RPC
contract is
[`simrep_shared/simrep_runtime.hpp`](simrep_shared/simrep_runtime.hpp).

Microsoft contracts used by the fixture:

- [Disallowing Fast I/O in a pre-operation callback](https://learn.microsoft.com/windows-hardware/drivers/ifs/disallowing-a-fast-i-o-operation-in-a-preoperation-callback-routine)
- [FltGetDestinationFileNameInformation](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltgetdestinationfilenameinformation)
- [FltSetInformationFile](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltsetinformationfile)
- [FltGetTunneledName](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltgettunneledname)
