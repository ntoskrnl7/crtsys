# Minifilter Helpers

[Back to NTL documentation](./README.md)

`ntl::flt` is the NTL entry and callback layer for Windows file-system
minifilters. Filter Manager still owns filter registration, instance ordering,
I/O callback dispatch, and teardown. NTL provides typed, non-owning facades and
keeps the callback tables alive for the registered filter.

See the [minifilter sample catalog](../../examples/minifilter) for readable
driver/app examples. The repository's mapping from WDK samples to verified NTL
mechanisms lives in the
[WDK minifilter sample coverage matrix](../../test/flt/WDK-SAMPLE-COVERAGE.md).

## Entry And Registration

Define `ntl::flt::main` and move a registration into the driver:

```cpp
#include <ntl/flt/all>

ntl::status ntl::flt::main(ntl::flt::driver& driver,
                           std::wstring_view) {
  ntl::flt::registration callbacks;
  callbacks.on(
      ntl::flt::operation::create,
      [](ntl::flt::create_callback_data, ntl::flt::related_objects,
         void*& completion) noexcept {
        completion = nullptr;
        return ntl::flt::pre_result::success_no_callback;
      });
  return driver.start(std::move(callbacks));
}
```

`ntl::flt::operation` names the Filter Manager operation without exposing raw
`IRP_MJ_*` values. A pre-operation-only registration can pass flags directly:

```cpp
callbacks.on(ntl::flt::operation::write, pre_write,
             ntl::flt::operation_flags::skip_paging_io);
```

No `nullptr` post-operation placeholder is required.
Only `operation::read` and `operation::write` have an overload that accepts
`operation_flags`. Passing read/write-only flags with another operation is a
compile-time error rather than a runtime registration failure.

For CMake, select the minifilter model explicitly:

```cmake
crtsys_add_driver(my_filter MINIFILTER NTL driver/main.cpp)
```

For a Visual Studio WDK project using the NuGet package:

The easiest setup is **Project Properties > Driver Settings > Driver Model**,
then set **crtsys WDM entry point** to **NTL Minifilter**. The package writes the
two properties below through its MSBuild targets:

```xml
<DriverType>WDM</DriverType>
<CrtSysIsMinifilter>true</CrtSysIsMinifilter>
<CrtSysUseNtlFltMain>true</CrtSysUseNtlFltMain>
```

These settings select `CrtSysNtlFltDriverEntry`, initialize the crtsys
runtime, link `fltmgr.lib`, call `ntl::flt::main`, and unregister the filter
before runtime teardown.

### Prebuilt ABI And Windows Target Versions

`crtsys.lib` is built with the Windows 8 Filter Manager declarations, while
`ntl::flt` supports Windows 7 consumers. The public `ntl::flt::driver` and
`ntl::flt::registration` layouts therefore do not embed version-conditional
WDK storage:

- the Win8-only section callback slot reserves the same erased storage in
  every Windows 7+ build, although its registration API is exposed only when
  `FLT_MGR_WIN8` is available;
- `FLT_REGISTRATION` is allocated indirectly by the consumer translation unit,
  filled with that consumer's `sizeof(FLT_REGISTRATION)` and
  `FLT_REGISTRATION_VERSION`, and destroyed through a consumer-owned deleter.

Consequently a Windows 7 minifilter project can link the matching prebuilt
NuGet library without changing C++ member offsets, while Windows 8+ projects
still receive the section-notification field. The compile/link test matrix
builds the two sides with `0x0601` and `0x0602` and encodes both public object
sizes in one required template symbol. A future conditional-layout regression
therefore fails the build instead of corrupting an object during unload.

## Legacy Control Devices

A minifilter can expose a normal WDM control device in addition to Filter
Manager communication ports. `driver::add_control_device()` connects the
existing typed `ntl::device` and `ntl::ioctl` facilities to the minifilter
entry, startup, failure, and unload lifetime:

```cpp
#include <ntl/flt/all>

struct cdo_state {
  std::atomic<bool> open{false};
};

struct ping_contract {
  using input_type = ping_request;
  using output_type = ping_reply;
  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x900;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr ULONG code =
      CTL_CODE(device_type, function, method, access);
};

using ping_ioctl = ntl::ioctl_from_contract<ping_contract>;

auto options = ntl::device_options()
                   .name(L"ProductControl")
                   .type(FILE_DEVICE_UNKNOWN);

auto status = driver.add_control_device<cdo_state>(
    std::move(options),
    [](ntl::device<cdo_state>& device) -> ntl::status {
      auto* state = &device.extension();
      device
          .on_create([state](ntl::irp& request) {
            state->open.store(true);
            request.succeed(FILE_OPENED);
          })
          .on_cleanup([state](ntl::irp& request) {
            state->open.store(false);
            request.succeed();
          })
          .on_close([](ntl::irp& request) { request.succeed(); })
          .on_device_control(
              [](const ntl::device_control::code& code,
                 const ntl::device_control::in_buffer& input,
                 ntl::device_control::out_buffer& output) {
                if (!ntl::is_ioctl<ping_ioctl>(code))
                  throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                       "unknown IOCTL");
                const auto* request =
                    ntl::ioctl_input_as<ping_ioctl>(input);
                if (!request)
                  throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                       "short input");
                ping_reply reply{/* ... */};
                if (!ntl::ioctl_write_output<ping_ioctl>(output, reply))
                  throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                       "short output");
              });
      return STATUS_SUCCESS;
    });
```

The short name publishes `\\DosDevices\\ProductControl`, which user mode opens
as `\\.\ProductControl`. An optional third argument can provide a complete DOS
link name. Use `device_options::security_descriptor()` with a product-owned
setup-class GUID when the installation does not provide the named legacy
device's ACL.

Registration is queued before `start()`. NTL calls `FltRegisterFilter`, creates
and configures the device, publishes its link, and then calls
`FltStartFiltering`. Startup failure and accepted unload remove the symbolic
link before deleting the device. Driver source does not assign
`DriverObject->MajorFunction` and does not include `fltKernel.h`; the NTL
minifilter entry dispatches create, cleanup, close, and device-control IRPs to
the typed handlers.

An open CDO may need to veto an optional minifilter unload. Track that policy
in the device state and return `STATUS_FLT_DO_NOT_DETACH` from
`registration::on_unload` while `!flags.mandatory()`. Once the unload callback
accepts the request, `ntl::flt::driver` owns the endpoint teardown; do not
delete the device independently.

The
[CDO runtime fixture](../../test/flt/runtime/CDO-README.md) verifies a
user-mode open, single-open policy, typed IOCTLs, a real optional-unload veto,
continued dispatch after the veto, cleanup/close, reopen, and final unload.

## Instances And Altitudes

A registered filter and an attached instance are different lifetime units.
The INF defines one or more named instance configurations, each with an
altitude and attachment flags. Filter Manager then creates a separate
`PFLT_INSTANCE` whenever one configuration is attached to a volume. The same
default definition can therefore produce many runtime instances across
volumes, and a volume can host multiple explicitly selected definitions at
different altitudes.

Altitude is installation metadata, not an argument to `registration::on()` or
`driver::start()`. Define production instances in both the downlevel
`Instances` and Windows 11 24H2 `Parameters\Instances` INF layouts:

```ini
HKR,"Parameters\Instances","DefaultInstance",0x00000000,%DefaultInstance%
HKR,"Parameters\Instances\%DefaultInstance%","Altitude",0x00000000,%DefaultAltitude%
HKR,"Parameters\Instances\%DefaultInstance%","Flags",0x00010001,0
HKR,"Parameters\Instances\%SecondaryInstance%","Altitude",0x00000000,%SecondaryAltitude%
HKR,"Parameters\Instances\%SecondaryInstance%","Flags",0x00010001,1
```

`Flags=0` permits automatic attachment. `Flags=1` suppresses automatic
attachment, allowing the named definition to be selected explicitly. A real
product must use altitudes allocated according to Microsoft's minifilter
altitude policy; the values in the sample are development-only.

Inside callbacks, `objects.instance()` identifies the exact attachment.
`instance_context<T>` consequently stores separate state for every
filter/volume/altitude attachment. Query its stable identity at
`PASSIVE_LEVEL` when diagnostics or policy need it:

```cpp
auto information = objects.instance().try_information();
if (information) {
  // information->name, altitude, volume_name, and filter_name are owning
  // strings and remain valid after the Filter Manager query buffer is freed.
}
```

Kernel code that already owns an `ntl::flt::volume` can manage an explicit
attachment through the filter facade:

```cpp
auto attached = driver.filter().try_attach(volume, L"Product Secondary");
if (!attached)
  return attached.status();

auto identity = attached->view().try_information();
// instance_ref releases the rundown reference; detaching is a separate action.
driver.filter().try_detach(volume, L"Product Secondary");
```

`try_attach()` reads the named definition's attributes from the installed INF.
`try_attach_at()` is available for explicit diagnostic placement, but named INF
definitions are the normal production contract. `try_instances()` enumerates
all current attachments owned by a filter. These helpers use owning STL
strings and vectors, so their NTL contract is `PASSIVE_LEVEL` even where the
underlying Filter Manager query permits `APC_LEVEL`.

Native Filter Manager registrations cannot be detached manually when
`InstanceQueryTeardownCallback` is null. NTL registers an allow callback by
default so `FilterDetach`, `FltDetachVolume`, and `filter::try_detach()` work
without an empty user callback. Use `on_instance_query_teardown()` when the
driver must inspect outstanding per-instance work and possibly return
`STATUS_FLT_DO_NOT_DETACH`. Use `deny_manual_detach()` for an unconditional
deny policy; `allow_manual_detach()` restores the NTL default.

### Per-Volume Metadata Coordination

`volume_metadata_file` is intended to live in a
`volume_metadata_instance_context<T>`. It owns the metadata handle and
referenced file object, closes them before an implicit or explicit volume
lock, dismount, or query-remove, and reopens only for the same triggering
volume `FILE_OBJECT`:

```cpp
struct volume_state {
  ntl::flt::volume_metadata_file metadata;

  volume_state(ntl::flt::related_objects objects, std::wstring&& path)
      : metadata(objects, std::move(path)) {}
};

inline constexpr ntl::flt::volume_metadata_instance_context<volume_state>
    volume_context;

auto state = objects.try_get_or_create(
    volume_context, objects,
    std::wstring(L"\\System Volume Information\\ProductMetadata"));
if (!state)
  return state.status();

ntl::flt::volume_metadata_open_options options;
options.create_system_volume_information = true;
return (*state)->metadata.try_open(options);
```

The specialized context always uses `NonPagedPoolNx` and deliberately has no
pool-selection constructor. This is required because `volume_metadata_file`
contains `ERESOURCE` and `KEVENT` objects whose storage must remain resident.
Do not place it in a generic paged context or other paged allocation.

`ntl::file::is_volume_open()` identifies volume handles.
`create_parameters::is_implicit_volume_lock_candidate()`,
`file_system_control_parameters::volume_request()`, and
`pnp_parameters::request()` classify the corresponding typed callbacks.
Call `try_release_for(objects.file())` before lock/dismount/query-remove and
`try_reopen_for(objects.file())` after a failed lock/dismount, successful
unlock, cleanup of an implicit lock, or cancel-remove.

A successful explicit unlock can invalidate the old volume instance.
`try_reopen_for()` then legitimately returns
`STATUS_INVALID_DEVICE_OBJECT_PARAMETER`, `STATUS_FILE_INVALID`, or
`STATUS_NO_MEDIA_IN_DEVICE`; instance setup on the remounted volume is
responsible for opening a new metadata owner. Code must not interpret those
statuses as proof that the metadata was permanently lost.

Snapshot coordination uses cross-thread-safe move-only tokens rather than
holding an `ERESOURCE` from pre-operation to a potentially different
post-operation thread. `try_begin_update()` admits an update.
`try_hold_updates_for_snapshot()` blocks new updates, waits for admitted work
to drain, and resumes updates when its token is destroyed from the post
callback. `device_control_parameters::is_snapshot_flush_and_hold()` recognizes
the snapshot request.

The
[MetadataManager runtime fixture](../../test/flt/runtime/METADATA-README.md)
verifies these paths on ReFS, including unlock-triggered remount and successful
dismount/detach/remount.

## Registration Callbacks Beyond I/O Operations

`registration` exposes the non-operation callback slots in the current
`FLT_REGISTRATION` through typed callback signatures. Raw `FLTAPI`, `PFLT_*`,
and `PVOID*` parameters remain inside NTL's native trampolines:

```cpp
callbacks
    .on_generate_file_name(generate_name)
    .on_normalize_name_component(normalize_component)
    .on_normalize_context_cleanup(cleanup_normalization);

#if FLT_MGR_LONGHORN
callbacks
    .on_transaction_notification(transaction_state_context,
                                 transaction_notification)
    .on_normalize_name_component_ex(normalize_component_ex);
#endif

#if FLT_MGR_WIN8
callbacks.on_section_notification(section_state_context, section_conflict);
#endif
```

The transaction and section overloads register the supplied context
declaration and bind its C++ state type to the callback. Do not also pass that
declaration to `registration::context()`. Version-dependent callbacks are
exposed only when the selected WDK supports them.

`name_control` wraps the output passed to a file-name generation callback.
`try_assign()` and `try_append()` grow it through
`FltCheckAndGrowNameControl`; Filter Manager continues to own the buffer.

### Name Provider Output

`name_generation_request` contains the instance, file, optional
operation-agnostic `callback_data_view`, and parsed option view.
`name_generation_output` contains the non-owning `name_control` and cache
decision:

```cpp
ntl::status generate_file_name(
    ntl::flt::name_generation_request request,
    ntl::flt::name_generation_output output) noexcept {
  if (!request.target_instance() || !request.target_file() || !output)
    return STATUS_INVALID_PARAMETER;

  auto status =
      output.name().try_assign(LR"(\Device\Volume\mapped.txt)");
  if (status.is_err())
    return status;

  output.set_cache(true);
  return STATUS_SUCCESS;
}
```

Name providers normally need the name reported by the provider below them.
Use `request.try_query_lower_name(options)` for that lookup. It selects
`FltGetFileNameInformation` when callback data is present and
`FltGetFileNameInformationUnsafe` otherwise, and always removes
`FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER` to prevent provider recursion:

```cpp
auto lower = request.try_query_lower_name(
    FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT |
    FLT_FILE_NAME_DO_NOT_CACHE);
if (!lower)
  return lower.status();
if (auto status = lower->try_parse(); status.is_err())
  return status;
```

Registration callbacks do not receive `related_objects`. When their
per-volume policy lives in an `instance_context<T>`, retrieve it directly from
the typed instance:

```cpp
auto mapping = request.target_instance().try_get(mapping_context);
if (!mapping)
  return mapping.status();
```

`name_normalization_request` similarly exposes the instance, optional file,
parent directory, volume prefix, component, and normalization flags.
`name_normalization_output` bounds writes to `FILE_NAMES_INFORMATION` and
provides a `normalization_context` slot shared across components. All these
objects are non-owning callback-duration views.

## Callback Facades

`ntl::flt::callback_data<Operation>` wraps `FLT_CALLBACK_DATA` without owning
it. Every operation has a public `<operation>_callback_data` alias, such as
`create_callback_data`, `write_callback_data`, and `cleanup_callback_data`,
which makes the operation part of the callback signature. The facade provides
I/O status, completion, operation-typed parameters, and RAII file-name
queries.
`ntl::flt::related_objects` exposes typed views of the filter, volume,
instance, and kernel `FILE_OBJECT`.

For low-level namespace virtualization,
`create_parameters::try_replace_target_name()` copies a replacement into the
target `FILE_OBJECT` through `IoReplaceFileObjectName`.
`clear_related_target()` prevents a full absolute replacement from being
combined with the old related file object. Most simulated-reparse filters
should use the higher-level helper below, which performs both operations and
sets the required completion status atomically.

### Simulated Reparse And Destination Repair

`try_complete_reparse()` accepts only a phase-typed pre-create operation. Pass
the complete normalized replacement, including its volume/device prefix, and
return `pre_result::complete` when it succeeds:

```cpp
auto status = ntl::flt::try_complete_reparse(
    ntl::flt::as_pre(data),
    LR"(\Device\HarddiskVolume3\physical-parent\physical-name)",
    ntl::flt::reparse_name_kind::absolute);
if (status.is_err()) {
  data.complete(status);
  return ntl::flt::pre_result::complete;
}
return ntl::flt::pre_result::complete;
```

The helper requires an IRP pre-create at `PASSIVE_LEVEL`. It replaces the file
object name, clears `RelatedFileObject` for an absolute replacement, and only
then completes the request with `STATUS_REPARSE` and `IO_REPARSE`.

`IRP_MJ_NETWORK_QUERY_OPEN` is normally Fast I/O and cannot return a simulated
reparse. Its typed parameter view exposes the underlying create options and
stack flags. If the queried name belongs to the mapping, return
`pre_result::disallow_fast_io` without changing `IoStatus`; Filter Manager
will issue the slow create path, where the normal reparse helper runs:

```cpp
ntl::flt::pre_result pre_network_query_open(
    ntl::flt::network_query_open_callback_data data,
    ntl::flt::related_objects, void*&) noexcept {
  auto parameters = data.parameters();
  if (data.is_fast_io_operation() && !parameters.paging_file() &&
      !parameters.open_by_file_id() && belongs_to_mapping(data)) {
    return ntl::flt::pre_result::disallow_fast_io;
  }
  return ntl::flt::pre_result::success_no_callback;
}
```

Rename and hard-link requests carry several related native buffer layouts.
`set_information_parameters::destination()` validates their length and
returns one read-only view with `kind()`, `information_class()`,
`root_directory()`, `name()`, `flags()`, `extended()`, and
`replace_if_exists()`. It supports the rename/link, extended, and
bypass-access-check information classes without exposing the native input
buffer:

```cpp
auto destination = data.parameters().destination();
if (!destination)
  return ntl::flt::pre_result::success_no_callback;

auto resolved = ntl::flt::try_query_destination_name(
    ntl::flt::as_pre(data),
    FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT |
        FLT_FILE_NAME_DO_NOT_CACHE);
if (!resolved)
  return ntl::flt::pre_result::success_no_callback;

auto replacement = rewrite_destination(*resolved);
(void)ntl::flt::try_reissue_destination(
    ntl::flt::as_pre(data), replacement);
return ntl::flt::pre_result::complete;
```

`try_reissue_destination()` preserves the information class and rename/link
flags, builds a bounded temporary buffer, calls `FltSetInformationFile` below
the current instance, frees the buffer, and completes the original operation
with the returned status. It must run at `PASSIVE_LEVEL`, and the replacement
must remain on the same volume.

Name tunneling is a pre/post lifetime contract. Retain the normalized
`name_information` obtained in pre-create or pre-set-information in typed
completion state, then call `try_get_tunneled_name(as_post(data), pre_name)`
from the matching post callback. Both the retained and returned objects are
RAII `name_information` owners. A successful result is allowed to contain an
empty owner when the file system has no tunneled name.

These APIs cover SimRep's create, network-query-open, destination, and tunneled
name mechanisms. They do not make directory enumeration, notification, query,
or file-system-control results namespace-aware by themselves. The
[SimRep runtime fixture](../../test/flt/runtime/SIMREP-README.md) shows a
complete isolated driver/app pair and its VM assertions, including both the
success-without-a-tunneled-name case and an 8.3 alias recreated into its
preserved long name.

The
[NameChanger runtime fixture](../../test/flt/runtime/NAME-CHANGER-README.md)
shows the additional directory-control work required for a namespace graft.
Its callbacks stay typed at the registration boundary, while the verifier
uses `NtQueryDirectoryFile` directly to request ten directory layouts and
exercise every layout accepted by the target filesystem/OS. It also covers
patterns, continuation state, small buffers, single-entry returns, restart
scans, and record-chain integrity. This is test coverage for the fixture's
explicitly listed layouts, not a promise that arbitrary directory information
classes share the same binary layout.

### Post-Create File-Open Cancellation

An infected or otherwise disallowed file may already have been opened by the
lower stack when its successful post-create callback runs. Use the
phase-typed helper to undo that open and replace the original create result:

```cpp
void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects) noexcept {
  if (data.io_status().is_err() || data.io_status() == STATUS_REPARSE)
    return;

  if (scan_file(objects) == verdict::infected)
    (void)ntl::flt::try_cancel_file_open(ntl::flt::as_post(data));
}
```

`try_cancel_file_open()` accepts only `post_operation<operation_id::create>`.
It requires `PASSIVE_LEVEL`, a successful non-reparse create, an error status
such as `STATUS_ACCESS_DENIED`, and a file object for which a user handle has
not yet been created. It calls `FltCancelFileOpen()` and completes the
original callback data with zero information. A pre-create operation, raw
callback data, a success completion status, or a file with
`FO_HANDLE_CREATED` is rejected.

The helper changes the result of this open; it does not establish a persistent
policy for later opens. Perform the decision while the synchronized
post-create callback still owns the operation. Microsoft's
[`FltCancelFileOpen`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcancelfileopen)
contract documents the same timing restriction.

### Delete Disposition and Cleanup Confirmation

A delete request is not proof that a file was deleted. It can fail, be
cleared by a later request, race another request, or remain pending until the
last handle is cleaned up. NTL exposes the request without exposing its native
callback buffer:

```cpp
ntl::flt::pre_result pre_set_information(
    ntl::flt::set_information_callback_data data,
    ntl::flt::related_objects objects,
    ntl::flt::completion_slot<delete_state> &completion) noexcept {
  const auto disposition = data.parameters().disposition();
  if (!disposition)
    return ntl::flt::pre_result::success_no_callback;

  auto state = objects.try_get_or_create(my_stream_context);
  if (!state)
    return ntl::flt::pre_result::success_no_callback;

  if (completion.try_emplace(
          std::move(*state), disposition.state_kind(),
          disposition.delete_requested()).is_err()) {
    return ntl::flt::pre_result::success_no_callback;
  }
  return ntl::flt::pre_result::synchronize;
}
```

`disposition_information_view` validates both
`FileDispositionInformation` and `FileDispositionInformationEx`, copies the
small value, and provides `delete_requested()`, `extended()`,
`state_kind()`, `flags()`, `posix_semantics()`,
`force_image_section_check()`, `on_close()`, and
`ignore_readonly_attribute()`. It deliberately has no native-buffer accessor.
An extended request carrying `FILE_DISPOSITION_ON_CLOSE` controls the
delete-on-close state; other legacy or extended requests control ordinary
delete disposition. Create callbacks can inspect the initial state directly
with `data.parameters().delete_on_close()`.

Only update tracked state after a successful set-information post callback.
If disposition operations overlap, their completion order does not reliably
describe the eventual state. Mark the stream uncertain and confirm it at
cleanup instead of treating the last observed post callback as authoritative.

`try_query_cleanup_deletion()` accepts only a phase-typed post-cleanup
operation and must run at `PASSIVE_LEVEL`. A typical pre-cleanup callback
retains the stream context and returns `pre_result::synchronize`; its post
callback can then query:

```cpp
void post_cleanup(
    ntl::flt::cleanup_callback_data data,
    ntl::flt::related_objects,
    ntl::flt::completion_ref<cleanup_state> completion) noexcept {
  auto deletion =
      ntl::flt::try_query_cleanup_deletion(ntl::flt::as_post(data));
  if (!deletion)
    return;

  if (*deletion == ntl::flt::cleanup_deletion_state::deleted)
    notify_once(*completion);
}
```

The helper translates `STATUS_FILE_DELETED` from
`FltQueryInformationFile(FileStandardInformation)` into `deleted`, a
successful query into `present`, and preserves every other failure. The
[delete runtime fixture](../../test/flt/runtime/DELETE-README.md) provides a
complete driver/app example with delete cancellation, create-time
delete-on-close, extended flags, pending handles, a deterministic disposition
race, and separate alternate-stream versus whole-file assertions.

### Lower-Stack Operation Status

`callback_data<Operation>::try_request_operation_status()` is the typed
counterpart of `FltRequestOperationStatusCallback`. Call it only from the
pre-operation callback of an IRP-based request. It observes the value returned
when the lower stack's `IoCallDriver` returns; it is not a replacement for the
operation's final post-operation `IoStatus`.

The facility is intentionally narrow because most filters do not need it. Its
usual cases are oplock FSCTLs and directory-change notifications, where
`STATUS_PENDING` says the lower stack accepted an asynchronous request:

```cpp
struct notify_status_state {
  explicit notify_status_state(ULONG length) noexcept : length(length) {}
  ~notify_status_state() noexcept = default;

  ULONG length;
};

void observe_notify_status(
    ntl::flt::operation_status_snapshot<
        ntl::flt::operation_id::directory_control> snapshot,
    ntl::flt::related_objects objects,
    ntl::status operation_status,
    notify_status_state& state) noexcept {
  if (operation_status == STATUS_PENDING &&
      snapshot.parameters().is_notify() &&
      snapshot.parameters().length() == state.length) {
    // The lower file-system stack accepted this notification request.
  }
}

ntl::flt::pre_result pre_directory_control(
    ntl::flt::directory_control_callback_data data,
    ntl::flt::related_objects,
    void*&) noexcept {
  auto parameters = data.parameters();
  if (data.is_irp_operation() && parameters.is_notify()) {
    (void)data.try_request_operation_status(
        &observe_notify_status, parameters.length());
  }
  return ntl::flt::pre_result::success_no_callback;
}
```

The callback receives an operation-typed, read-only parameter snapshot rather
than a raw `PFLT_IO_PARAMETER_BLOCK`. Filter Manager captures that snapshot at
the request call, so later parameter changes are not reflected in it.

The state object is constructed in nonpaged pool and NTL destroys it exactly
once: immediately if the request fails, or after the status callback returns.
Its constructor and destructor must therefore be `noexcept`, and its owned
resources must be safe to release at `IRQL <= APC_LEVEL`. There is one pool
allocation for the stateful overload. If no request state is needed,
`try_request_operation_status<&callback>()` uses a compile-time callback and
does not allocate. Ordinary callbacks that do not ask for operation status
also pay no allocation cost. Native restrictions still apply: the call is
invalid outside pre-operation processing and for `IRP_MJ_CLOSE`.

### Self-Issued Filter Manager I/O

`try_allocate_callback_data()` creates callback data for I/O issued by the
minifilter itself and returns a move-only `callback_data_owner`. Select the
generated operation with `prepare()` and configure only its typed parameter
view before choosing synchronous or asynchronous execution:

```cpp
auto request =
    ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
if (!request)
  return request.status();

FILE_STANDARD_INFORMATION information{};
auto query = request->prepare(ntl::flt::operation::query_information);
query.parameters().length(sizeof(information));
query.parameters().information_class(FileStandardInformation);
query.parameters().buffer(&information);
return request->perform_synchronously();
```

Asynchronous submission takes a compile-time completion callback. The
completion receives ownership directly and `try_perform_asynchronously()`
returns a copyable operation handle:

```cpp
void complete_io(ntl::flt::callback_data_owner data) noexcept {}

auto operation = request->try_perform_asynchronously<complete_io>();
if (!operation)
  return operation.status();

// May race safely with inline or concurrent completion.
(void)operation->cancel();
return operation->wait();
```

The native
[`FltPerformAsynchronousIo`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltperformasynchronousio)
contract invokes the completion even when submission returns an error. The
original owner therefore becomes empty immediately before submission. The
operation handle coordinates `FltCancelIo` with completion so callback data
cannot be freed while a concurrent cancellation still uses it. `wait()` also
provides an explicit teardown boundary.

An overload accepts a borrowed typed context:

```cpp
void complete_io(ntl::flt::callback_data_owner data,
                 io_state* state) noexcept;

io_state* nonpaged_state = acquire_long_lived_io_state();
auto operation =
    request->try_perform_asynchronously<complete_io>(nonpaged_state);
```

The completion can run at `IRQL <= DISPATCH_LEVEL`; its context and accessed
memory must remain valid and nonpaged until completion. Do not call
`FltSetCancelCompletion` on callback data returned by
`FltAllocateCallbackData`: that routine is for an existing incoming
IRP-based operation being posted to a work queue, not a generated operation
whose backing IRP has not yet been built.

### Generic Lambdas And Editor Completion

The C++ compiler accepts generic callbacks and instantiates their `auto`
parameters from the operation tag:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](auto data, auto objects, auto& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

`test/flt/compile/operation_callback.cpp` keeps this form under compile
coverage. It is not the recommended spelling for application code because
current Visual Studio IntelliSense, the Microsoft C/C++ VS Code extension,
and clangd can infer or diagnose the contextual type but may still return no
member list at `data.` inside the generic lambda. This is an editor/language
server completion limitation, not a failure of NTL callback typing or C++
compilation.

Public samples therefore spell out the operation-specific aliases so member
completion, navigation, and parameter discovery remain available:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects,
       void*& completion_context) noexcept {
      completion_context = nullptr;
      return ntl::flt::pre_result::success_no_callback;
    });
```

Use the operation-specific parameter view instead of selecting a member of the
native `FLT_PARAMETERS` union manually:

```cpp
callbacks.on(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data, ntl::flt::related_objects,
       void*&) noexcept {
      const auto write = data.parameters();

      const ULONG length = write.length();
      const LARGE_INTEGER offset = write.byte_offset();
      (void)length;
      (void)offset;
      return ntl::flt::pre_result::success_no_callback;
    });
```

The operation passed to `registration::on()` determines the callback-data C++
type. A create registration accepts `create_callback_data`, whose
`parameters()` returns only `create_parameters`; a write registration accepts
`write_callback_data`, whose `parameters()` returns only `write_parameters`.
An operation/callback-data mismatch is rejected while resolving the `on()`
overload, so IDE semantic analysis can report the error at the registration
call instead of waiting for the callback body or runtime registration.
The generic spelling `callback_data<operation::close>` remains available when
code generation or generic registration logic needs it.
Code cannot ask create callback data for read or write parameters. Parameter
setters call `FltSetCallbackDataDirty()` automatically. `native_iopb()` remains
available only as an explicit escape hatch for specialized Filter Manager
operations.

`name_information` owns the reference returned by
`FltGetFileNameInformation`. Call `try_parse()` before reading parsed path
components. Destruction calls `FltReleaseFileNameInformation`. Do not call
`FltUnregisterFilter` yourself after `driver.start()` succeeds; the NTL entry
layer owns unregister and crtsys runtime teardown.

## Normal-Only Post Callbacks And Completion State

A post callback without `void* CompletionContext` and
`post_operation_flags` declares that it contains normal I/O completion work:

```cpp
callbacks.on(
    ntl::flt::operation::create,
    [](ntl::flt::create_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::create_callback_data data,
       ntl::flt::related_objects objects) noexcept {
      // Called only after normal completion, never while the instance drains.
      if (data.io_status().is_ok())
        record_success(objects);
    });
```

NTL returns `FLT_POSTOP_FINISHED_PROCESSING` after this callback. It skips the
callback when Filter Manager supplies `FLTFL_POST_OPERATION_DRAINING`. This
form is appropriate when pre-operation does not acquire anything that must be
released by post-operation.

Use `on_with_completion<T>()` when pre-operation must carry owned per-I/O state
to post-operation:

```cpp
struct request_state {
  std::uint32_t original_length;

  explicit request_state(std::uint32_t length) noexcept
      : original_length(length) {}
  ~request_state() noexcept = default;
};

callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_slot<request_state>& completion) noexcept {
      if (completion.try_emplace(data.parameters().length()).is_err())
        return ntl::flt::pre_result::success_no_callback;
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::write_callback_data data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> completion) noexcept {
      if (completion && data.io_status().is_ok())
        record_bytes(completion->original_length);
    });
```

`completion_slot<T>` allocates `T` from `NonPagedPoolNx`. The object transfers
to Filter Manager only when pre-operation returns
`success_with_callback` or `synchronize`; all other pre-operation results cause
immediate destruction. `completion_ref<T>` is a non-owning view valid only for
the current post callback.

NTL owns and destroys the object in every path:

| Path | User post callback | Destruction |
| --- | --- | --- |
| Pre does not request post | Not called | Before pre-operation returns |
| Normal completion | Called | Immediately after the post callback |
| Normal completion requests WhenSafe | Immediate and safe callbacks called | Immediately after the safe callback |
| WhenSafe cannot be scheduled | Immediate callback only | Before NTL resumes completion |
| Instance draining, flags-less typed post | Skipped | In the draining trampoline |
| Instance draining, flags-aware typed post | Called with `flags.draining()` | Immediately after the callback |

The completion-state destructor is the right place to release resources owned
solely by that I/O, such as pool memory, context references, object references,
rundown protection, and outstanding-I/O counters. It is not a replacement for
normal completion logic: do not inspect final I/O data, update success metrics,
or start new work from the destructor.

`T` must be nothrow-constructible for the arguments passed to `try_emplace()`
and nothrow-destructible. Its destructor and every RAII member must be legal at
the post callback's possible IRQL, including draining. Keep the state
nonpageable and nonblocking. Use the flags-aware low-level callback when the
driver must observe draining explicitly, return native post results, pend
completion, or coordinate ownership that cannot be represented by the typed
state.

## Post And When-Safe Processing

Use the same `on()` API when a post-operation path has both an IRQL-independent
part and work that requires `IRQL <= APC_LEVEL`:

```cpp
callbacks.on(
    ntl::flt::operation::read,
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects) noexcept {
      return ntl::flt::pre_result::success_with_callback;
    },
    [](ntl::flt::read_callback_data,
       ntl::flt::related_objects objects) noexcept {
      // A: work that is always legal in the native post callback.
      completed.fetch_add(1, std::memory_order_relaxed);

      // B: finish APC-safe work here when possible. Request WhenSafe only
      // when the current IRQL or another runtime condition requires it.
      if (ntl::is_irql_at_most(ntl::irql::apc)) {
        inspect_contexts(objects);
        return ntl::flt::post_continuation::finished;
      }
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::safe_read_operation,
       ntl::flt::related_objects objects) noexcept {
      // NTL dispatches this through FltDoCompletionProcessingWhenSafe.
      // This callback runs at IRQL <= APC_LEVEL.
      inspect_contexts(objects);
    });
```

The immediate post callback chooses the path for each completion. Return
`post_continuation::finished` after doing all required work or when no safe work
is needed. Return `post_continuation::when_safe` to make NTL call
`FltDoCompletionProcessingWhenSafe`. This supports the usual A-then-conditional
B pattern without forcing every completion through the safe callback.
The user callbacks do not need `post_operation_flags`: NTL sees the native flags
in its trampoline and skips both callbacks while the instance drains.

Typed completion state can follow the same path:

```cpp
callbacks.on_with_completion<request_state>(
    ntl::flt::operation::write,
    pre_write,
    [](ntl::flt::write_callback_data,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_nonpageable_fields(state);
      return ntl::flt::post_continuation::when_safe;
    },
    [](ntl::flt::safe_post_operation<ntl::flt::operation_id::write>,
       ntl::flt::related_objects,
       ntl::flt::completion_ref<request_state> state) noexcept {
      inspect_pageable_fields(state);
    });
```

The same `completion_ref<T>` remains valid through the safe callback. NTL
destroys it after the immediate callback returns `finished`, after the safe
callback returns, during draining, or when Filter Manager cannot invoke or
queue the safe callback.

The safe callback receives `safe_post_operation<Operation>`, not ordinary
`callback_data`. Call `operation.data()` for normal typed status and parameter
access. APIs that require the WhenSafe IRQL contract accept only this wrapper,
so a native post callback cannot accidentally call them.

The flags-aware low-level overload remains available when the driver must see
`FLT_POST_OPERATION_FLAGS`, manage a native `void* CompletionContext`, or
return a native post result. In that form the driver must handle draining
itself: release only completion state, skip normal completion work, and never
request WhenSafe while `flags.draining()` is true.

For read and write registrations with a safe callback NTL automatically sets
`skip_paging_io`, because paging I/O cannot be posted this way. When the
immediate callback requests WhenSafe for a non-IRP operation, or Filter Manager
cannot queue the work, the safe callback is not called and NTL finishes the
operation. The safe callback returns `void`, so it cannot accidentally pend
completion again. Mandatory work must therefore be completed in the immediate
callback whenever it is already legal there; use the safe callback for work
that is valid to omit when Filter Manager cannot provide a safe continuation.

## Typed File-System Contexts

Declare each state type once, register the declaration, and use the same
declaration to retrieve or create state from a callback:

```cpp
struct file_state {
  std::atomic<std::uint32_t> writes{0};

  file_state() noexcept = default;
  ~file_state() noexcept = default;
};

inline constexpr ntl::flt::file_context<file_state> file_state_context{};

ntl::flt::registration callbacks;
callbacks.context(file_state_context);

// Use this after a successful create, or in another legal <= APC_LEVEL path.
auto state = objects.try_get_or_create(file_state_context);
if (state)
  (*state)->writes.fetch_add(1, std::memory_order_relaxed);
```

The public declarations describe the lifetime unit:

| Declaration | State belongs to |
| --- | --- |
| `volume_context<T>` | one volume |
| `instance_context<T>` | one minifilter instance attached to a volume |
| `file_context<T>` | one file within an instance, shared across its streams |
| `stream_context<T>` | one file stream within an instance |
| `stream_handle_context<T>` | one open `FILE_OBJECT` for a stream |
| `transaction_context<T>` | one file-system transaction within an instance |
| `section_context<T>` | one Filter Manager data-scan section (Windows 8+) |

`try_get<T>()` is exposed as `objects.try_get(declaration)`.
`try_get_or_create()` performs the complete `FltGet*Context` /
`FltAllocateContext` / `FltSet*Context(KEEP_IF_EXISTS)` sequence. When two
threads race to initialize the same object, it returns the winner's referenced
context and destroys the losing allocation. `context_ref<T>` is move-only and
calls `FltReleaseContext` automatically. Filter Manager invokes the registered
cleanup callback when the final reference disappears; that callback runs the
C++ context destructor.

`objects.try_remove(declaration)` atomically detaches an installed volume,
instance, file, stream, stream-handle, or transaction context and returns the
removed reference. Releasing a `context_ref` alone does not detach a context.

Context constructors and destructors must be `noexcept`, and over-aligned or
larger-than-`MAXUSHORT` state is rejected at compile time. A `volume_context`
defaults to `context_pool::nonpaged` (`NonPagedPool`), which is the only pool
Filter Manager permits for that scope. Its public declaration accepts no pool
argument, so selecting `PagedPool` or `NonPagedPoolNx` is a compile-time error;
only its diagnostic pool tag is configurable. Every other scope defaults to
`context_pool::nonpaged_nx` (`NonPagedPoolNx`) and may explicitly select
`nonpaged`, `nonpaged_nx`, or `paged`. NTL permits one C++ state declaration per
context scope because Filter Manager attaches at most one context of that type
for each filter instance/object relationship.

### Transaction Contexts

Create or retrieve the transaction state in an ordinary operation callback,
then enlist it for the notifications selected by the mask:

```cpp
auto state = objects.try_get_or_create(transaction_state_context);
if (!state)
  return state.status();

auto status = objects.try_enlist(
    *state, ntl::flt::transaction_notifications::commit_finalize |
                ntl::flt::transaction_notifications::rollback);
```

The notification routine registered with
`registration::on_transaction_notification(transaction_state_context, ...)`
receives a checked, borrowed `context_view<T>` without acquiring or releasing
a reference:

```cpp
ntl::status transaction_notification(
    ntl::flt::related_objects objects,
    ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  state->last_notification = static_cast<ULONG>(notifications);
  return STATUS_SUCCESS;
}
```

Use `objects.try_remove(transaction_state_context)` only when policy requires
early detachment. The returned `context_ref` owns the removed reference.

### Data-Scan Sections

Section contexts are allocated for `FltCreateSectionForDataScan`; they are not
installed with a `FltSetXxxContext` routine. For a kernel-owned section, use
this lifetime order:

```cpp
callbacks.on_section_notification(
    section_state_context,
    [](ntl::flt::instance instance,
       ntl::flt::context_view<section_state> state,
       ntl::flt::callback_data_view data) noexcept {
      state->conflict_seen = true;
      return ntl::status::ok();
    });
```

1. Register the instance with `try_register_for_data_scan()`.
2. Allocate a `section_context<T>`.
3. Initialize `OBJECT_ATTRIBUTES` with `OBJ_KERNEL_HANDLE`.
4. Call `try_create_data_scan_section()`.
5. Map and scan the section as required.
6. Close the returned handle with `ZwClose()`.
7. Dereference the returned section object with `ObDereferenceObject()`.
8. Close the Filter Manager association with `close_data_scan_section()`.
9. Release the remaining `context_ref`.

```cpp
auto context = ntl::flt::try_allocate_section_context(
    objects.filter().native_handle(), section_state_context);
if (!context)
  return context.status();

OBJECT_ATTRIBUTES attributes;
InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE,
                           nullptr, nullptr);

ntl::flt::data_scan_section_options options;
options.object_attributes = &attributes;

auto section = ntl::flt::try_create_data_scan_section(
    objects.instance(), objects.file(), *context, options);
if (!section)
  return section.status();

// Map and scan here.
(void)ZwClose(section->handle);
ObDereferenceObject(section->object);
return ntl::flt::close_data_scan_section(*context);
```

The section-conflict callback may run before
`try_create_data_scan_section()` returns, so initialize callback-visible state
in the section context before the create call.

A create without `OBJ_KERNEL_HANDLE` returns a handle in the current process.
Passing that handle to user mode changes where and when it can safely be
closed. `data_scan_section` is therefore a native output record rather than an
automatic RAII closer. This order follows the native
[`FltCreateSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltcreatesectionfordatascan)
and
[`FltCloseSectionForDataScan`](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltclosesectionfordatascan)
contracts.

### Scanner and AvScan Lifecycle Composition

The section, transaction, communication, swapped-buffer, pending-I/O, and
post-create cancellation APIs are deliberately independent. A scanner policy
composes them according to the operation's lifetime:

1. In pre-create, select the target and request a synchronized post callback.
2. In successful post-create, scan through a read-only data-scan section and
   use `try_cancel_file_open()` when the app returns an infected verdict.
3. Attach a `stream_handle_context<T>` to writable opens.
4. In non-paging pre-write, defer to `PASSIVE_LEVEL`, capture isolated pages
   with `try_swap_io_buffers()`, and transfer that owner into
   `pending_pre_operation_queue`.
5. Send a typed driver-to-app request. Resume an allowed write so the queue
   installs its resident pages, or cancel an infected write with
   `STATUS_ACCESS_DENIED`.
6. In pre-cleanup, rescan the final file contents. Cleanup cannot be failed to
   undo data already written through a mapped or paging path, so record or
   remediate the result according to product policy.
7. For a transacted file, create/enlist a `transaction_context<T>` and handle
   commit-finalize and rollback without mixing transaction ownership into the
   wire format.

Do not discard a `swapped_io_buffers` capture and then resume a pended write
with its original user pointer from an arbitrary worker. Keep the isolated
owner in the pending queue; its allow path supplies resident pages to the
lower stack, and its deny/cancel path releases them without exposing a stale
user buffer.

Service availability is a separate policy choice. Microsoft's Scanner sample
is fail-open while its user service is unavailable so the service and system
can bootstrap. If adopting that policy, check the connection before mapping,
deferring, or pending any caller buffer, and treat a transport failure after a
request is already pending consistently.

The complete
[Scanner/AvScan runtime fixture](../../test/flt/runtime/SCANNER-README.md)
shows this composition with an x64 driver, x64 and WOW64 apps, open/write
allow and deny decisions, mapped-write cleanup detection, TxF
commit/rollback, section-conflict-safe state, and balanced ownership.

### Cache Names At Create Time

Do not make a new name query in every read, write, cleanup, or close callback.
Successful post-create callbacks run at `PASSIVE_LEVEL`, so they are a natural
place to query the final normalized name once and retain it in typed contexts:

```cpp
auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                FLT_FILE_NAME_QUERY_DEFAULT);
if (name && name->try_parse().is_ok()) {
  auto file_name = name->try_reference();
  if (file_name) {
    (void)objects.try_get_or_create(file_state_context,
                                    std::move(*file_name));
  }
  (void)objects.try_get_or_create(stream_state_context, std::move(*name));
}
```

`name_information::try_reference()` calls
`FltReferenceFileNameInformation`, so each context owns an independent RAII
reference. A file context is shared across alternate streams; use its parsed
file components such as `parent_directory()` and `final_component()`. Keep the
complete normalized name, including any alternate-stream component, in the
stream context. Later callbacks can retrieve these contexts without issuing a
new name query. This is especially useful in read, write, pre-cleanup, and
pre-close paths. A fresh `FltGetFileNameInformation` request after cleanup may
fail with `STATUS_FLT_INVALID_NAME_REQUEST`, while the retained reference
remains available until its context is destroyed.

The retained information is a snapshot, not a rename subscription. A filter
that tracks renames or hard links must update or invalidate its context in the
corresponding set-information paths. The underlying Filter Manager name
information is allocated from paged pool, so dereference it only at
`IRQL <= APC_LEVEL`; retaining it avoids a query but does not make paged memory
legal at `DISPATCH_LEVEL`.

Context access follows Filter Manager's native restrictions:

- allocation, get, set, and support queries require `IRQL <= APC_LEVEL`;
- file, stream, and stream-handle contexts cannot be queried or set in
  pre-create or post-close callbacks;
- paging files and some third-party file systems may not support these
  contexts; `supports()` and `try_get_or_create()` preserve that result;
- `file_context` uses `FltSupportsFileContextsEx` so Filter Manager's
  single-stream file-system compatibility path remains available.

See Microsoft's [minifilter context management](https://learn.microsoft.com/windows-hardware/drivers/ifs/managing-contexts-in-a-minifilter-driver)
and [file-system context support](https://learn.microsoft.com/windows-hardware/drivers/ifs/file-system-support-for-contexts)
for the underlying object and file-system contracts.

## Typed Communication Ports

`ntl::flt::communication_server` carries the same `ntl::rpc::method`
descriptors used by an IOCTL RPC endpoint over a Filter Manager communication
port. The descriptor defines the stable method ID, argument types, response
type, serialized request limit, and decode-allocation limit; the transport is
still `FltCreateCommunicationPort` and `FilterSendMessage`.

Declare the contract once in a header shared by the driver and app. The kernel
expansion registers the callback; the user-mode expansion generates
`connect()`, `query_count()`, and `query_count_async()`:

```cpp
NTL_FLT_RPC_BEGIN(product_messages, L"\\ProductPort")

NTL_FLT_ADD_CALLBACK_ID(
    product_messages, 0xA50, std::uint32_t(std::uint32_t), query_count,
    [](std::uint32_t base) noexcept { return base + 1; })

NTL_FLT_RPC_END(product_messages)
```

Register the generated server before starting the filter:

```cpp
auto messages = product_messages::make_server();
auto status = driver.add_communication_port(product_messages::port_name,
                                             std::move(messages));
if (status.is_err())
  return status;

return driver.start(std::move(callbacks));
```

The user-mode client uses the same descriptor:

```cpp
auto client = product_messages::connect();
const auto answer = product_messages::query_count(client, std::uint32_t{41});
```

The explicit client argument is intentional: calls, shared regions, and
connection state all stay on the same Filter Manager connection instead of
silently reconnecting for each generated function.

### Async, Cancellation, And Coroutines

`FilterSendMessage` itself is synchronous. NTL's async method therefore uses
it only to submit a bounded request. The minifilter executes the callback on a
PASSIVE_LEVEL worker and sends the result through `FltSendMessage`; a bounded
set of overlapped `FilterGetMessage` receivers dispatches completions by
fixed-width request ID. Multiple calls can be outstanding on one client, while
`communication_port_options::max_pending_async()` bounds pending work and
copied request memory independently for each connection (64 by default):

Kernel async callbacks and driver-to-app delivery are queued with
`queue_filter_work_item`, not a raw executive work item. Filter Manager rejects
new work after deletion begins and retains the filter rundown reference until
each successfully queued callback actually returns.

```cpp
auto first = product_messages::query_count_async(client, 40);
auto second = product_messages::query_count_async(client, 41);

const auto first_result = first.get();
const auto second_result = second.get();
```

`ready()`, `wait_for()`, `wait()`, `cancel()`, and `get()` follow the same
one-result ownership model as `ntl::rpc::async_call`. Cancellation is
cooperative: `cancel()` marks the kernel request and a long-running callback
checks `communication_context::cancellation_requested()`. Completion can win
the race with cancellation, so callbacks must not assume that a cancellation
request undoes work already committed.

In C++20, include `<ntl/flt/coroutine>` and move the generated async call into
`co_await`:

```cpp
task<std::uint32_t> query(ntl::flt::communication_client& client) {
  co_return co_await product_messages::query_count_async(client, 41);
}
```

The generated `_async` function also accepts `std::stop_token` in C++20. The
token requests the same cooperative cancellation. C++14 and later retain the
ordinary synchronous and explicit async-call APIs.

The driver can host multiple named ports. Each connection has independent
pinned-memory quotas and method dispatch. NTL closes listeners before
`FltUnregisterFilter`; Filter Manager then invokes the disconnect callback for
every remaining client before the server state is destroyed.

### Contract, Sessions, Notifications, And Streams

Use `NTL_FLT_RPC_BEGIN_CONTRACT` when the app and minifilter should validate
their shared contract. `connect()` queries the endpoint and rejects a
different application version or missing capability bits before the first
method call.

The contract query also advertises every app-to-driver method,
driver-to-app method, notification, and stream with fixed-width request,
response, decode-allocation, batch, priority limits, and an automatically
derived wire-schema fingerprint. Apps can fail before normal traffic begins
when a shared descriptor does not match the running driver:

```cpp
auto contract = client.query_contract();
client.require_method(product_messages::query_count_method);
client.require_client_method(product_messages::app_transform_method);
client.require_notification(product_messages::progress_notification);
client.require_stream(product_messages::numbers_stream);
```

The explicit application contract version identifies semantic API or lifecycle
changes. NTL derives each member fingerprint from its serialized field types
and checks it when that typed member is used. User-defined types reuse their
existing `static serialize(Archive&, Self&)` field list, including in C++14.
Compiler type names are excluded because they are not stable across x86, x64,
or MSVC toolsets.

### Connections And Driver-To-App Requests

`communication_server::on_connect()` can accept or reject a client before the
port becomes usable and can attach typed application state. `on_disconnect()`
observes the committed disconnect. A copied `communication_connection` stays
safe to inspect after disconnect: `connected()` becomes false and targeted
operations fail instead of dereferencing the old Filter Manager port.

```cpp
struct client_state {
  std::uint32_t accepted_calls = 0;
};

server
    .on_connect([](ntl::flt::communication_connection& connection) {
      connection.state(std::make_shared<client_state>());
      return ntl::status::ok();
    })
    .on_disconnect([](ntl::flt::communication_connection& connection) {
      connection.clear_state();
    });
```

The same port is bidirectional. Register a driver-originated method before the
server starts, register its handler in the app, and invoke it through the
connection associated with the current callback:

```cpp
// Driver setup and callback path.
server.register_client_method(product_messages::app_transform_method);
auto result = publisher.try_request(
    context.connection(), product_messages::app_transform_method,
    std::chrono::seconds(2), value);

// App setup before it invokes the driver path that makes the request.
client.on_request(product_messages::app_transform_method,
                  [](std::uint32_t value) noexcept { return value * 3; });
```

The app keeps four overlapped `FilterGetMessage` receives pending. One receive
path dispatches asynchronous method completions, notifications, stream
records, and driver-originated requests. App request handlers can therefore
run concurrently and must protect shared state accordingly.

Every connection opens one server session. A transient notification is
best-effort and is consumed without an ACK:

```cpp
auto wait = product_messages::progress_async(client);
// The driver calls publisher.try_notify(progress_notification, event).
auto event = wait.get();
```

`publisher.try_notify(notification, payload)` broadcasts to current
subscribers. `publisher.try_notify(connection, notification, payload)` targets
one connected subscriber. Targeted delivery returns `STATUS_NOT_FOUND` when
the connection has disconnected or has not subscribed to that channel; it
does not silently become a broadcast.

A reliable notification remains in the session's bounded queue until the app
acknowledges its sequence. `max_reliable_records()` and
`max_reliable_bytes()` apply independently to each session, so a stalled app
cannot grow kernel memory without bound:

```cpp
auto delivery = product_messages::progress_reliable(client);
process(delivery.payload());
client.acknowledge(product_messages::progress_notification, delivery);
```

Normal handle or process termination removes its session. To reconnect without
losing unacknowledged reliable records, explicitly detach it first. This
invalidates the current client, cancels its pending operations, and returns the
token used by `resume()`:

```cpp
auto token = client.preserve_session();
auto resumed = product_messages::resume(token);
auto replayed = product_messages::progress_reliable(resumed);
resumed.acknowledge(product_messages::progress_notification, replayed);
```

Reliable queues live only while the minifilter remains loaded unless external
storage is explicitly installed. A custom store can derive from
`communication_notification_store`. NTL also provides the optional
`registry_notification_store`:

```cpp
auto key = ntl::registry_key::create(
    L"\\Registry\\Machine\\Software\\ProductFilterNotifications",
    KEY_QUERY_VALUE | KEY_SET_VALUE);
if (!key)
  return key.status();

messages.notification_storage(
    std::make_shared<ntl::flt::registry_notification_store>(
        std::move(*key)));
```

It maintains one bounded `REG_BINARY` value per reconnectable session. NTL
calls `persist()` before a record becomes visible, removes it after an explicit
ACK, and calls `restore()` when an in-memory session token is absent. No
storage I/O occurs unless a store is installed.

Storage hooks run at `PASSIVE_LEVEL`, without NTL connection or session locks,
and must be thread-safe. The `communication_record_view::data` range is valid
only for the hook call. A store used with batch delivery must override
`acknowledge_batch()` as an atomic operation; NTL deliberately does not emulate
a batch by making several externally visible single-record commits. Explicitly
closing a session invokes `erase_session()`.

Reliable receive can request bounded batches:

```cpp
auto wait = client.receive_reliable_batch_async(
    product_messages::progress_notification);
auto batch = wait.get();
consume(batch.values());
client.acknowledge(product_messages::progress_notification, batch);
```

`max_reliable_records()` and `max_reliable_bytes()` bound each session.
`reliable_overflow(reject_newest)` preserves queued data and rejects the new
record. `reliable_overflow(drop_oldest)` evicts the oldest record that is not
being delivered. The latter is intentionally unavailable when an external
store is installed because NTL cannot assume that an application store treats
eviction as an ACK.

A typed stream combines one bounded app-to-driver upload method with one
ordered, ACK-based driver-to-app channel. Upload batching serializes a bounded
vector in one request; every download record is retained until acknowledged:

```cpp
auto stream = product_messages::numbers(client);
stream.write_batch(std::vector<std::uint32_t>{10, 20});

for (int index = 0; index != 2; ++index) {
  auto reply = stream.read();
  consume(reply.payload());
  stream.acknowledge(reply);
}
stream.close();
```

Use `read_batch()` or `read_batch_async()` to receive up to the descriptor's
bounded batch size in one Filter Manager message and acknowledge that batch in
one transport operation. A batch may contain one record; callers must not
assume that concurrent writes are always coalesced.

The driver callback receives `communication_stream_context<Stream>`. Its
`try_write()`, `try_complete()`, and `try_fail()` methods enqueue data or a
terminal record into the same bounded reliable queue. Cross-channel priority
selects which queued channel is delivered next; records within one stream keep
their sequence and ACK contract.

Notification waits, reliable waits, stream writes, and stream reads expose
synchronous and asynchronous forms. In C++20 their asynchronous values can be
`co_await`ed and can be bound to `std::stop_token`; C++14 and later retain the
explicit wait/cancel/get API.

### Authorization And Resource Limits

`communication_port_options` bounds connections, pending async calls,
retained sessions, reliable records/bytes, and pinned regions. Set a custom
Filter Manager port security descriptor when the default ACL is not the
product policy. For method-specific policy, `on_authorized()` and
`NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID` run before request deserialization:

```cpp
NTL_FLT_ADD_AUTHORIZED_CALLBACK_ID(
    product_messages, 0xA20, reply_type(request_type), privileged_call,
    [](const ntl::flt::communication_context& context) noexcept {
      return authorize_process(context.requestor_process_id());
    },
    [](request_type request) { return handle(request); })
```

The policy returns `NTSTATUS` or `ntl::status`. It can use the captured original
requestor process ID and `session_id()` with normal Windows kernel security
APIs. Authorization happens before serializer allocation, so a rejected caller
cannot force the protected method to decode an attacker-controlled container.

### Shared Regions And Cross-Bitness

Large or frequently exchanged fixed-layout data can use a connection-bound
shared region instead of serializing every record. The app allocates and
registers the region, then sends `ntl::ipc::buffer_token` values through a
normal typed method:

```cpp
auto region = client.register_shared_region(bytes);
auto input = region.token(0, input_bytes);
auto output = region.token(output_offset, output_bytes);
client.invoke(process_buffers, input, output);
```

The kernel callback resolves each token with the required access:

```cpp
messages.on(process_buffers,
    [](const ntl::flt::communication_context& context,
       ntl::ipc::buffer_token input,
       ntl::ipc::buffer_token output) {
      auto readable = context.try_resolve(
          input, ntl::ipc::region_access::driver_read);
      auto writable = context.try_resolve(
          output, ntl::ipc::region_access::driver_write);
      // Attach an ntl::ipc::shared_ring or validate another fixed layout.
    });
```

Tokens contain fixed-width region identity, offset, and length fields, never a
process pointer. The same protocol therefore supports an x86 app connected to
an x64 minifilter. Resolution checks connection ownership, generation, bounds,
and access before exposing the pinned range. Destroy or `close()` the
`registered_port_region` only after all sync or async calls using its tokens
have completed;
unregistration releases the MDL before the app frees the virtual allocation.

For variable-size payloads, `registered_port_region::make_buffer_pool()` creates
an `ntl::ipc::shared_buffer_pool`. Its move-only leases reserve subranges and
return the range to the pool on destruction; the driver still receives and
validates an ordinary `buffer_token`. The registered port region must outlive
all leases. The runtime fixture exercises allocation, release, reuse, and
coalescing through the minifilter adapter.

Message callbacks run at `PASSIVE_LEVEL`. NTL copies Filter Manager's
unaligned user buffers under structured exception handling before decoding,
enforces the method's request/allocation limits, and rejects malformed framing
or stale region tokens without tearing down a healthy connection. The runtime
fixture tests typed calls, concurrent async completion dispatch, cooperative
cancellation, shared rings, malformed and oversized framing, invalid
shared-region range/access/quota tokens, stale-token rejection, and connection
reuse after rejected requests. Its advanced communication tests also cover
typed connection state, connect/disconnect observation, on-connect rejection,
connection/session quotas, driver-to-app requests, targeted-send subscription
checks, detailed contract mismatch, reliable record/byte quotas, reliable
batch ACK and duplicate rejection, external restore hooks, drop-oldest
overflow, stream failure completion, C++20 coroutine calls, and concurrent
connect/call/close stress. These test-only paths stay out of the onboarding
sample.

## IRQL And Lifetime

The callback's native Filter Manager contract always wins:

- Driver entry, registration, instance setup, and unload are
  `PASSIVE_LEVEL` paths.
- Some pre-operation callbacks can run at `APC_LEVEL`.
- A post-operation callback can run at `DISPATCH_LEVEL`. Keep pageable code,
  blocking operations, exceptions, and general CRT/STL work out of that path.
- `try_query_name()` does not make a name query legal. Call it only on an I/O
  path where `FltGetFileNameInformation` is permitted.
- Facades are non-owning unless their documentation explicitly says RAII.
  Do not retain callback data or related-object views after the callback.

The buildable [minifilter sample catalog](../../examples/minifilter) separates
six onboarding concerns:

- [`basic`](../../examples/minifilter/basic) covers typed
  create/read/write/cleanup callbacks and a cached stream-name context;
- [`control-device`](../../examples/minifilter/control-device) maps the WDK
  CDO lifecycle to typed device/IOCTL handlers and an unload veto;
- [`communication`](../../examples/minifilter/communication) isolates Filter
  Manager port, typed RPC, notification, stream, and shared-ring behavior;
- [`operation-log`](../../examples/minifilter/operation-log) maps the MiniSpy
  flow to typed callbacks, a bounded queue, and typed user-mode draining;
- [`swap-buffers`](../../examples/minifilter/swap-buffers) swaps pre-WRITE
  input and pre-READ output for a demonstration transform; and
- [`volume-metadata`](../../examples/minifilter/volume-metadata) maps the
  MetadataManager lifecycle to typed volume-lock, snapshot, PnP, shutdown, and
  teardown handling.

The catalog also maps each readable example to the corresponding
[runtime fixture](../../test/flt/runtime). Those fixtures retain large
failure, filesystem, WOW64, and Driver Verifier matrices. In particular,
NameChanger stays in the fixture because create redirection, name-provider
callbacks, directory enumeration, query-information, rename/hard-link
destinations, notifications, and name-bearing FSCTL results form one
cross-cutting contract; a tiny subset would be easier to read but would not be
a truthful NameChanger implementation.

Microsoft references:

- [Creating an INF file for a minifilter driver](https://learn.microsoft.com/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver)
- [Load order groups and altitudes](https://learn.microsoft.com/windows-hardware/drivers/ifs/load-order-groups-and-altitudes-for-minifilter-drivers)
- [FltAttachVolume](https://learn.microsoft.com/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltattachvolume)
