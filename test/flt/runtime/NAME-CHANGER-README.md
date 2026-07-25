# NTL NameChanger Runtime Test

This fixture turns the name-provider registration examples into a loadable
minifilter and verifier app. It is modeled after the core namespace-graft
contract in Microsoft's `filesys/miniFilter/NameChanger` sample:

```text
\crtsys-namechanger-visible\graft
                  |
                  +----> \crtsys-namechanger-store\real
```

The visible `graft` directory does not exist on disk. The driver redirects
pre-create requests below that path to the backing directory, rejects direct
opens through the backing mapping, generates visible names for objects opened
through the graft, injects `graft` while enumerating the visible parent, and
hides `real` while enumerating the backing parent. Mapping strings come from
the installed service's `Parameters` key and each attached NTFS/ReFS volume
owns a typed `instance_context`.

The implementation is deliberately written through the public typed surface:

- `create_callback_data` and `create_parameters` perform create redirection;
- `name_generation_request::try_query_lower_name()` queries below the current
  provider without recursion;
- `instance::try_get(mapping_context)` supplies per-instance mapping state to
  registration callbacks;
- `name_normalization_output` bounds component output;
- a typed stream-handle context records whether the synthetic enumeration
  entry was already returned;
- typed completion state carries `prepared_output_buffer` for query-information,
  directory-enumeration, and FSCTL output into a safe post callback;
- query-information, USN, and directory results are accessed only through the
  prepared scoped visitor, with no driver-owned MDL and no post-operation
  `FltLockUserBuffer`.

Query-information output deliberately uses the same prepared visitor rather
than `swapped_io_buffers`. A query pre-callback can run at `APC_LEVEL`, while
replacement-buffer allocation requires `PASSIVE_LEVEL`; treating that
allocation failure as the operation result would incorrectly turn a valid
`NtQueryInformationFile` request into `STATUS_INVALID_DEVICE_STATE`.

There are no raw WDK callback signatures in the fixture. Native Filter Manager
calls still appear inside callback bodies where the operation itself has no
NTL ownership wrapper yet, such as opening/querying a parent for component
normalization.

## Covered contract

The app has two layers. `name_changer_app/main.cpp` owns the fixture lifecycle
and verifies open/read/write/name-provider behavior.
`name_changer_app/directory_tests.cpp` calls `NtQueryDirectoryFile` directly
so the test can select information classes and flags that
`std::filesystem::directory_iterator` does not expose.

The app creates only the visible parent and the physical backing directory,
captures the backing entry before loading the filter, and then verifies:

1. the backing payload is readable through the nonexistent visible graft;
2. the backing path cannot be opened directly while the filter is active;
3. a typed post-create name query invokes the registered provider and reports
   a visible rather than backing name;
4. visible-parent enumeration injects exactly one `graft`, backing-parent
   enumeration never exposes `real`, and child enumeration through `graft`
   returns the backing payload;
5. all of those enumeration checks are requested against
   `FileDirectoryInformation`, `FileFullDirectoryInformation`,
   `FileBothDirectoryInformation`, `FileNamesInformation`,
   `FileIdBothDirectoryInformation`, and
   `FileIdFullDirectoryInformation`, plus
   `FileIdExtdDirectoryInformation`, `FileIdExtdBothDirectoryInformation`,
   `FileId64ExtdDirectoryInformation`, and
   `FileId64ExtdBothDirectoryInformation`. A target may reject an information
   class before the filter sees it; every class accepted by the target is
   required to pass the full checks. The older Win11KD guest accepts eight of
   ten on both NTFS and ReFS 3.7 and rejects the two `FileId64Extd*` classes.
   The Windows 11 25H2 build 26200 guest accepts and E2E-verifies all ten on
   both NTFS and ReFS 3.14 with x64 and WOW64 clients. The same four-way matrix
   passes under Standard Driver Verifier, including File System Filter
   Verification. The app reports every rejected class and emits
   `file_id_64_extd_directory_classes_e2e_verified=2 supported=2 total=2`
   when both classes complete the filesystem-backed checks;
6. the injected entry preserves the backing entry's stable timestamps, sizes,
   attributes, EA size, and file ID while clearing a backing short name.
   `LastAccessTime` is intentionally not compared across separate queries
   because the query itself may update it;
7. exact and wildcard patterns (`g*`, `?raft`, and `*aft`) include the graft,
   a nonmatching pattern excludes it, and the first request's pattern remains
   effective on continuation requests where Windows omits `FileName`;
8. small buffers paginate without duplicates, `SL_RETURN_SINGLE_ENTRY`
   terminates correctly, and `SL_RESTART_SCAN` makes the graft visible once
   again;
9. hiding `real` does not truncate enumeration when it is the first, middle,
   last, or only record returned in a buffer;
10. guard bytes, record bounds, name lengths, alignment, and
    `NextEntryOffset` chains remain valid;
11. `FileNameInformation`, `FileNormalizedNameInformation`, and
    `FileAllInformation` expose the visible path. Alternate-name output is
    checked when the target filesystem supplies it;
12. rename and hard-link destinations below the visible graft are translated
    and reissued to the backing directory, preserving file identity, and
    `FileHardLinkInformation` reports the visible parent ID and `graft`
    component instead of the physical `real` link;
13. asynchronous directory change notifications report visible child names
    for create and rename operations;
14. a write through the graft reaches the backing directory;
15. `FSCTL_READ_FILE_USN_DATA` rewrites the backing component and parent file
    ID on 512 consecutive calls while cycling through eight aligned
    user-buffer offsets;
16. `FSCTL_ENUM_USN_DATA` and `FSCTL_READ_USN_JOURNAL` rewrite both USN v2 and
    v3 records, including the eight-byte continuation prefix;
17. `FSCTL_FIND_FILES_BY_SID` translates a common-ancestor result, injects
    `graft\payload.txt` when the visible parent is queried, suppresses
    `real\payload.txt` when the backing parent is queried, and preserves
    continuation state across both 64 KiB and 128-byte output buffers;
18. `FSCTL_LOOKUP_STREAM_FROM_CLUSTER` returns the visible stream path and
    recomputes its linked-entry offsets and required buffer size;
19. network-query-open retries through the ordinary create path instead of
    bypassing namespace translation;
20. unloading reveals the physical file and proves no visible directory was
    accidentally created.

The driver also runs deterministic synthetic append/hide/record-chain tests
for both `FileId64Extd*` layouts every time it loads. It refuses to start
unless both pass, and the app requires
`synthetic_file_id_64_layouts=2/2`. This covers the buffer transformation code
even on a target that rejects those information classes. It complements, but
does not replace, the Windows 11 25H2 filesystem-backed E2E result.

The NTFS/ReFS matrix is a portability and ownership test, not a claim that
ReFS enables an operation unavailable on NTFS. Both filesystems expose the
same eight classes on the older Win11KD target and the same ten classes on the
Windows 11 25H2 target. ReFS remains valuable because a second filesystem can
exercise different result-buffer, metadata, and Filter Manager paths and
expose assumptions hidden by an NTFS-only run.

The observed filesystem split is intentional:

- NTFS supports and passes `FSCTL_ENUM_USN_DATA`,
  `FSCTL_READ_USN_JOURNAL`, `FSCTL_FIND_FILES_BY_SID`, and
  `FSCTL_LOOKUP_STREAM_FROM_CLUSTER`. Find-by-SID requires quota tracking on
  the dedicated NTFS test volume because NTFS uses the quota index for
  owner-SID lookup.
- ReFS 3.7 and 3.14 support and pass `FSCTL_READ_USN_JOURNAL` but return
  `ERROR_INVALID_FUNCTION` for the enum-USN, Find-by-SID, and cluster-lookup
  calls on the tested guests. The app reports those outcomes as `UNSUPPORTED`
  and does not require rewrite counters for operations the filesystem
  rejected.

This remains a focused NameChanger coverage fixture rather than a claim that
every filesystem accepts every API. The current Find-by-SID implementation
grows one internal real-mapping batch up to 16 MiB and then paginates its
translated records through the caller's stream-handle context. A result set
larger than that bound remains explicit follow-up coverage. The Microsoft
sample itself lists `FSCTL_TXFS_LIST_TRANSACTION_LOCKED_FILES` as future work,
so this fixture does not present it as an implemented sample behavior.

## Build and run

Build the existing minifilter runtime project:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Build-CrtSys.ps1 `
  -Project flt-runtime -Architecture x64 `
  -PlatformToolset v145 -Configuration Debug
```

Install and test only in a disposable, test-signing-enabled VM. Stage and sign
`crtsys_flt_name_changer_runtime_test.sys`, its INF, and generated catalog,
install the INF, enable Driver Verifier for the driver, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\ci\Run-FltNameChangerRuntimeTests.ps1 `
  -AppPath test\flt\runtime\build_x64_v145\Debug\crtsys_flt_name_changer_runtime_test_app.exe `
  -VolumeRoot C:\ `
  -RequireVerifier
```

The selected volume must be NTFS or ReFS. The runner requires elevation,
checks test-signing and the installed service, optionally checks the Driver
Verifier target, and verifies that the app leaves the filter unloaded.
Before an NTFS Find-by-SID run, enable quota tracking once on the disposable
guest test volume, for example `fsutil quota track D:`. Do not apply that
setup command to a host volume.
