#include <ntl/flt/all>
#include <ntl/except>
#include <ntl/pool_allocator>

#include "../scanner_shared/scanner_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

using namespace crtsys_flt_scanner_runtime_test;

std::atomic<NTSTATUS> last_scan_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> instances_registered{0};
std::atomic<std::uint32_t> policy_requests{0};
std::atomic<std::uint32_t> policy_failures{0};
std::atomic<std::uint32_t> open_scans{0};
std::atomic<std::uint32_t> open_denied{0};
std::atomic<std::uint32_t> write_scans{0};
std::atomic<std::uint32_t> writes_allowed{0};
std::atomic<std::uint32_t> writes_denied{0};
std::atomic<std::uint32_t> cleanup_scans{0};
std::atomic<std::uint32_t> cleanup_infected{0};
std::atomic<std::uint32_t> sections_created{0};
std::atomic<std::uint32_t> sections_mapped{0};
std::atomic<std::uint32_t> sections_closed{0};
std::atomic<std::uint32_t> section_conflicts{0};
std::atomic<std::uint32_t> section_contexts_created{0};
std::atomic<std::uint32_t> section_contexts_destroyed{0};
std::atomic<std::uint32_t> pended_writes{0};
std::atomic<std::uint32_t> resumed_writes{0};
std::atomic<std::uint32_t> cancelled_writes{0};
std::atomic<std::uint32_t> deferred_writes{0};
std::atomic<std::uint32_t> handle_contexts_created{0};
std::atomic<std::uint32_t> handle_contexts_destroyed{0};
std::atomic<std::uint32_t> transaction_contexts_created{0};
std::atomic<std::uint32_t> transaction_contexts_destroyed{0};
std::atomic<std::uint32_t> transaction_enlistments{0};
std::atomic<std::uint32_t> transaction_commits{0};
std::atomic<std::uint32_t> transaction_rollbacks{0};

std::mutex scanner_target_lock;
ntl::flt::communication_connection scanner_target;
ntl::flt::communication_publisher scanner_publisher;

constexpr std::wstring_view test_path =
    LR"(\crtsys-flt-scanner-runtime)";
constexpr std::wstring_view scan_extension = L".scan";
constexpr std::size_t maximum_pending_writes = 32;
constexpr ULONG deferred_write_pool_tag = ntl::pool_tag("sPdN");

wchar_t fold_case(wchar_t value) noexcept {
  return RtlUpcaseUnicodeChar(value);
}

bool equal_name(std::wstring_view left, std::wstring_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    if (fold_case(left[index]) != fold_case(right[index]))
      return false;
  }
  return true;
}

bool path_prefix(std::wstring_view path, std::wstring_view prefix) noexcept {
  if (path.size() < prefix.size() ||
      !equal_name(path.substr(0, prefix.size()), prefix)) {
    return false;
  }
  return path.size() == prefix.size() || path[prefix.size()] == L'\\';
}

bool has_extension(std::wstring_view value,
                   std::wstring_view extension) noexcept {
  return value.size() >= extension.size() &&
         equal_name(value.substr(value.size() - extension.size()), extension);
}

bool is_scanned_name(ntl::flt::name_information &name) noexcept {
  if (name.try_parse().is_err() || name.name().size() < name.volume().size())
    return false;
  const auto relative = name.name().substr(name.volume().size());
  return path_prefix(relative, test_path) &&
         has_extension(name.final_component(), scan_extension);
}

bool scanner_connected() noexcept {
  std::lock_guard<std::mutex> guard(scanner_target_lock);
  return scanner_target.connected();
}

ntl::flt::communication_connection capture_scanner_target() noexcept {
  try {
    std::lock_guard<std::mutex> guard(scanner_target_lock);
    return scanner_target;
  } catch (...) {
    return {};
  }
}

struct instance_state {
  explicit instance_state(PFLT_INSTANCE value) noexcept
      : native(value),
        initialization(pending_writes.initialize(value,
                                                 maximum_pending_writes)) {}

  ntl::flt::pending_pre_operation_queue pending_writes;
  PFLT_INSTANCE native = nullptr;
  ntl::status initialization;
};

inline constexpr ntl::flt::instance_context<instance_state>
    scanner_instance_context{};

using instance_state_ref =
    ntl::flt::context_ref<instance_state, ntl::flt::context_scope::instance>;

struct handle_state {
  handle_state() noexcept {
    handle_contexts_created.fetch_add(1, std::memory_order_release);
  }
  ~handle_state() noexcept {
    handle_contexts_destroyed.fetch_add(1, std::memory_order_release);
  }

  std::atomic<bool> rescan_required{true};
};

inline constexpr ntl::flt::stream_handle_context<handle_state>
    scanner_handle_context{};

struct section_state {
  section_state() noexcept {
    section_contexts_created.fetch_add(1, std::memory_order_release);
  }
  ~section_state() noexcept {
    section_contexts_destroyed.fetch_add(1, std::memory_order_release);
  }

  std::atomic<bool> aborted{false};
};

inline constexpr ntl::flt::section_context<section_state>
    scanner_section_context{};

struct transaction_state {
  transaction_state() noexcept {
    transaction_contexts_created.fetch_add(1, std::memory_order_release);
  }
  ~transaction_state() noexcept {
    transaction_contexts_destroyed.fetch_add(1, std::memory_order_release);
  }
};

inline constexpr ntl::flt::transaction_context<transaction_state>
    scanner_transaction_context{};

struct policy_result {
  ntl::status status{STATUS_UNSUCCESSFUL};
  scan_verdict verdict = scan_verdict::clean;
};

policy_result request_policy(const scan_request &request) noexcept {
  policy_requests.fetch_add(1, std::memory_order_release);
  const auto target = capture_scanner_target();
  if (!target.connected()) {
    policy_failures.fetch_add(1, std::memory_order_release);
    last_scan_status.store(STATUS_DEVICE_NOT_CONNECTED,
                           std::memory_order_release);
    return {STATUS_DEVICE_NOT_CONNECTED, scan_verdict::clean};
  }

  auto response = scanner_publisher.try_request(
      target, scan_payload, std::chrono::seconds(3), request);
  if (!response) {
    policy_failures.fetch_add(1, std::memory_order_release);
    last_scan_status.store(static_cast<NTSTATUS>(response.status()),
                           std::memory_order_release);
    return {response.status(), scan_verdict::clean};
  }
  if (*response != scan_verdict::clean &&
      *response != scan_verdict::infected) {
    policy_failures.fetch_add(1, std::memory_order_release);
    last_scan_status.store(STATUS_DATA_ERROR, std::memory_order_release);
    return {STATUS_DATA_ERROR, scan_verdict::clean};
  }

  last_scan_status.store(STATUS_SUCCESS, std::memory_order_release);
  return {STATUS_SUCCESS, *response};
}

policy_result scan_file(ntl::flt::related_objects objects,
                        scan_stage stage) noexcept {
  if (!objects.filter() || !objects.instance() || !objects.file() ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) {
    return {STATUS_INVALID_DEVICE_STATE, scan_verdict::clean};
  }
  if (!scanner_connected())
    return {STATUS_DEVICE_NOT_CONNECTED, scan_verdict::clean};

  if (stage == scan_stage::open)
    open_scans.fetch_add(1, std::memory_order_release);
  else if (stage == scan_stage::cleanup)
    cleanup_scans.fetch_add(1, std::memory_order_release);

  scan_request request{};
  request.stage = stage;

  auto context = ntl::flt::try_allocate_section_context(
      objects.filter().native_handle(), scanner_section_context);
  if (!context)
    return {context.status(), scan_verdict::clean};

  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, nullptr,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                             nullptr, nullptr);
  ntl::flt::data_scan_section_options options;
  options.object_attributes = &attributes;
  auto section = ntl::flt::try_create_data_scan_section(
      objects.instance(), objects.file(), *context, options);
  if (!section) {
    if (static_cast<NTSTATUS>(section.status()) == STATUS_END_OF_FILE)
      return request_policy(request);
    last_scan_status.store(static_cast<NTSTATUS>(section.status()),
                           std::memory_order_release);
    policy_failures.fetch_add(1, std::memory_order_release);
    return {section.status(), scan_verdict::clean};
  }
  sections_created.fetch_add(1, std::memory_order_release);

  HANDLE process = nullptr;
  PVOID view = nullptr;
  SIZE_T view_bytes = 0;
  OBJECT_ATTRIBUTES process_attributes{};
  InitializeObjectAttributes(&process_attributes, nullptr, OBJ_KERNEL_HANDLE,
                             nullptr, nullptr);
  CLIENT_ID client_id{};
  client_id.UniqueProcess = PsGetCurrentProcessId();
  constexpr ACCESS_MASK process_vm_operation = 0x0008;
  NTSTATUS map_status =
      ZwOpenProcess(&process, process_vm_operation, &process_attributes,
                    &client_id);
  if (NT_SUCCESS(map_status)) {
    map_status =
        ZwMapViewOfSection(section->handle, process, &view, 0, 0, nullptr,
                           &view_bytes, ViewUnmap, 0, PAGE_READONLY);
  }

  if (NT_SUCCESS(map_status) && view) {
    const auto file_bytes =
        section->file_size.QuadPart > 0
            ? static_cast<unsigned long long>(section->file_size.QuadPart)
            : 0ull;
    const auto bytes = (std::min)(
        scan_payload_capacity,
        (std::min)(static_cast<std::size_t>(view_bytes),
                   static_cast<std::size_t>((std::min)(
                       file_bytes,
                       static_cast<unsigned long long>(
                           (std::numeric_limits<std::size_t>::max)())))));
    const auto copied = ntl::seh::try_except([&] {
      RtlCopyMemory(request.payload.data(), view, bytes);
      request.bytes = static_cast<std::uint32_t>(bytes);
    });
    if (!std::get<0>(copied))
      map_status = static_cast<NTSTATUS>(std::get<1>(copied));
    else
      sections_mapped.fetch_add(1, std::memory_order_release);
  }

  NTSTATUS close_status = STATUS_SUCCESS;
  if (view && process) {
    const NTSTATUS status = ZwUnmapViewOfSection(process, view);
    if (!NT_SUCCESS(status))
      close_status = status;
  }
  if (process) {
    const NTSTATUS status = ZwClose(process);
    if (!NT_SUCCESS(status) && NT_SUCCESS(close_status))
      close_status = status;
  }
  if (section->handle) {
    const NTSTATUS status = ZwClose(section->handle);
    if (!NT_SUCCESS(status) && NT_SUCCESS(close_status))
      close_status = status;
  }
  if (section->object)
    ObDereferenceObject(section->object);
  const ntl::status association =
      ntl::flt::close_data_scan_section(*context);
  if (association.is_err() && NT_SUCCESS(close_status))
    close_status = static_cast<NTSTATUS>(association);
  if (NT_SUCCESS(close_status))
    sections_closed.fetch_add(1, std::memory_order_release);

  if (!NT_SUCCESS(map_status)) {
    last_scan_status.store(map_status, std::memory_order_release);
    policy_failures.fetch_add(1, std::memory_order_release);
    return {map_status, scan_verdict::clean};
  }
  if (!NT_SUCCESS(close_status)) {
    last_scan_status.store(close_status, std::memory_order_release);
    policy_failures.fetch_add(1, std::memory_order_release);
    return {close_status, scan_verdict::clean};
  }
  if ((*context)->aborted.load(std::memory_order_acquire)) {
    last_scan_status.store(STATUS_CANCELLED, std::memory_order_release);
    policy_failures.fetch_add(1, std::memory_order_release);
    return {STATUS_CANCELLED, scan_verdict::clean};
  }
  return request_policy(request);
}

ntl::status section_conflict(
    ntl::flt::instance, ntl::flt::context_view<section_state> state,
    ntl::flt::callback_data_view) noexcept {
  if (!state)
    return STATUS_INVALID_PARAMETER;
  state->aborted.store(true, std::memory_order_release);
  section_conflicts.fetch_add(1, std::memory_order_release);
  return STATUS_SUCCESS;
}

ntl::status transaction_notification(
    ntl::flt::related_objects, ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  if (!state)
    return STATUS_OBJECT_TYPE_MISMATCH;
  if (ntl::flt::any(notifications &
                    ntl::flt::transaction_notifications::commit_finalize)) {
    transaction_commits.fetch_add(1, std::memory_order_release);
  }
  if (ntl::flt::any(notifications &
                    ntl::flt::transaction_notifications::rollback)) {
    transaction_rollbacks.fetch_add(1, std::memory_order_release);
  }
  return STATUS_SUCCESS;
}

void enlist_transaction(ntl::flt::related_objects objects) noexcept {
  if (!objects.transaction())
    return;
  auto state = objects.try_get_or_create(scanner_transaction_context);
  if (!state)
    return;
  const ntl::status enlisted = objects.try_enlist(
      *state, ntl::flt::transaction_notifications::commit_finalize |
                  ntl::flt::transaction_notifications::rollback);
  if (enlisted.is_ok())
    transaction_enlistments.fetch_add(1, std::memory_order_release);
}

ntl::flt::pre_result
pre_create(ntl::flt::create_callback_data data,
           ntl::flt::related_objects) noexcept {
  if ((data.parameters().create_options() & FILE_DIRECTORY_FILE) != 0)
    return ntl::flt::pre_result::success_no_callback;
  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT |
                                  FLT_FILE_NAME_DO_NOT_CACHE);
  if (!name || !is_scanned_name(*name))
    return ntl::flt::pre_result::success_no_callback;
  return ntl::flt::pre_result::synchronize;
}

void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects) noexcept {
  const NTSTATUS status = static_cast<NTSTATUS>(data.io_status());
  if (!NT_SUCCESS(status) || status == STATUS_REPARSE)
    return;

  const policy_result result = scan_file(objects, scan_stage::open);
  if (result.status.is_ok() &&
      result.verdict == scan_verdict::infected) {
    const ntl::status cancelled =
        ntl::flt::try_cancel_file_open(ntl::flt::as_post(data));
    if (cancelled.is_ok())
      open_denied.fetch_add(1, std::memory_order_release);
    return;
  }

  if (objects.file().can_write())
    (void)objects.try_get_or_create(scanner_handle_context);
  enlist_transaction(objects);
}

struct deferred_write_context {
  explicit deferred_write_context(instance_state_ref &&value) noexcept
      : instance(std::move(value)) {}
  instance_state_ref instance;
};

NTSTATUS pend_write(ntl::flt::write_callback_data data,
                    instance_state_ref instance) noexcept {
  auto captured =
      ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(data));
  if (!captured)
    return static_cast<NTSTATUS>(captured.status());
  if (!captured->input() || !captured->input()->data()) {
    (void)captured->complete();
    return STATUS_INVALID_DEVICE_STATE;
  }

  scan_request request{};
  request.stage = scan_stage::write;
  const std::size_t bytes =
      (std::min)(scan_payload_capacity, captured->input()->size());
  RtlCopyMemory(request.payload.data(), captured->input()->data(), bytes);
  request.bytes = static_cast<std::uint32_t>(bytes);

  // Keep the isolated pages with the pending operation. On an allow verdict
  // resume() installs those resident pages for the lower stack; on deny,
  // cancel() releases them without exposing the original user buffer again.
  auto key = instance->pending_writes.pend(
      ntl::flt::as_pre(data), std::nullopt, std::move(*captured));
  if (!key)
    return static_cast<NTSTATUS>(key.status());
  pended_writes.fetch_add(1, std::memory_order_release);

  instance_state *const state = instance.get();
  auto worker_instance = instance.reference();
  auto work = [owner = std::move(worker_instance), request_key = *key,
               request = std::move(request)]() mutable noexcept {
    write_scans.fetch_add(1, std::memory_order_release);
    const policy_result result = request_policy(request);
    ntl::status finished{STATUS_UNSUCCESSFUL};
    if (result.status.is_ok() &&
        result.verdict == scan_verdict::infected) {
      finished =
          owner->pending_writes.cancel(request_key, STATUS_ACCESS_DENIED);
      if (static_cast<NTSTATUS>(finished) == STATUS_ACCESS_DENIED) {
        writes_denied.fetch_add(1, std::memory_order_release);
        cancelled_writes.fetch_add(1, std::memory_order_release);
      }
    } else {
      // Scanner transport failures are fail-open, matching the Microsoft
      // Scanner sample's bootstrapping behavior.
      finished = owner->pending_writes.resume(request_key);
      if (finished.is_ok()) {
        writes_allowed.fetch_add(1, std::memory_order_release);
        resumed_writes.fetch_add(1, std::memory_order_release);
      }
    }
  };
  const ntl::status queued =
      ntl::flt::queue_instance_work_item(state->native, std::move(work));
  if (queued.is_err())
    (void)state->pending_writes.cancel(*key, static_cast<NTSTATUS>(queued));

  // Ownership transferred to pending_writes even when work-item allocation
  // failed and the request has already been completed with an error.
  return STATUS_PENDING;
}

ntl::status
deferred_write(PFLT_CALLBACK_DATA native, void *opaque,
               ntl::flt::deferred_pre_completion &completion) noexcept {
  completion = {};
  auto *const context = static_cast<deferred_write_context *>(opaque);
  if (!context)
    return STATUS_INVALID_PARAMETER;
  auto instance = std::move(context->instance);
  ntl::pool_deleter<deferred_write_context>{deferred_write_pool_tag}(context);
  if (!instance)
    return STATUS_INVALID_DEVICE_STATE;
  return pend_write(ntl::flt::write_callback_data{native},
                    std::move(instance));
}

ntl::flt::pre_result
pre_write(ntl::flt::write_callback_data data,
          ntl::flt::related_objects objects) noexcept {
  if (data.parameters().length() == 0)
    return ntl::flt::pre_result::success_no_callback;
  // Match Scanner's fail-open bootstrapping behavior before acquiring,
  // deferring, or pending the caller's buffer.
  if (!scanner_connected())
    return ntl::flt::pre_result::success_no_callback;
  auto handle = objects.try_get(scanner_handle_context);
  if (!handle ||
      !(*handle)->rescan_required.load(std::memory_order_acquire)) {
    return ntl::flt::pre_result::success_no_callback;
  }
  if (data.is_fast_io_operation())
    return ntl::flt::pre_result::disallow_fast_io;

  auto instance = objects.try_get(scanner_instance_context);
  if (!instance) {
    data.complete(instance.status(), 0);
    return ntl::flt::pre_result::complete;
  }
  auto context = ntl::try_make_pool<deferred_write_context>(
      ntl::pool_kind::nonpaged, ntl::pool_option::none,
      deferred_write_pool_tag, std::move(*instance));
  if (!context) {
    data.complete(context.status(), 0);
    return ntl::flt::pre_result::complete;
  }
  auto *const raw_context = context->release();
  const ntl::status queued = ntl::flt::queue_pre_operation_at_passive(
      ntl::flt::as_pre(data), &deferred_write, raw_context);
  if (queued.is_err()) {
    ntl::pool_deleter<deferred_write_context>{deferred_write_pool_tag}(
        raw_context);
    data.complete(queued, 0);
    return ntl::flt::pre_result::complete;
  }
  deferred_writes.fetch_add(1, std::memory_order_release);
  return ntl::flt::pre_result::pending;
}

ntl::flt::pre_result
pre_cleanup(ntl::flt::cleanup_callback_data,
            ntl::flt::related_objects objects) noexcept {
  auto state = objects.try_get(scanner_handle_context);
  if (!state)
    return ntl::flt::pre_result::success_no_callback;

  if ((*state)->rescan_required.exchange(false, std::memory_order_acq_rel)) {
    const policy_result result = scan_file(objects, scan_stage::cleanup);
    if (result.status.is_ok() &&
        result.verdict == scan_verdict::infected) {
      cleanup_infected.fetch_add(1, std::memory_order_release);
    }
  }

  // Cleanup is this handle's final policy point. Detach explicitly so TxF
  // file-object retention cannot make ownership observations nondeterministic.
  auto removed = objects.try_remove(scanner_handle_context);
  (void)removed;
  return ntl::flt::pre_result::success_no_callback;
}

void reset_policy_counters() noexcept {
  last_scan_status.store(STATUS_NOT_SUPPORTED, std::memory_order_release);
  policy_requests.store(0, std::memory_order_release);
  policy_failures.store(0, std::memory_order_release);
  open_scans.store(0, std::memory_order_release);
  open_denied.store(0, std::memory_order_release);
  write_scans.store(0, std::memory_order_release);
  writes_allowed.store(0, std::memory_order_release);
  writes_denied.store(0, std::memory_order_release);
  cleanup_scans.store(0, std::memory_order_release);
  cleanup_infected.store(0, std::memory_order_release);
  sections_created.store(0, std::memory_order_release);
  sections_mapped.store(0, std::memory_order_release);
  sections_closed.store(0, std::memory_order_release);
  section_conflicts.store(0, std::memory_order_release);
  section_contexts_created.store(0, std::memory_order_release);
  section_contexts_destroyed.store(0, std::memory_order_release);
  pended_writes.store(0, std::memory_order_release);
  resumed_writes.store(0, std::memory_order_release);
  cancelled_writes.store(0, std::memory_order_release);
  deferred_writes.store(0, std::memory_order_release);
  handle_contexts_created.store(0, std::memory_order_release);
  handle_contexts_destroyed.store(0, std::memory_order_release);
  transaction_contexts_created.store(0, std::memory_order_release);
  transaction_contexts_destroyed.store(0, std::memory_order_release);
  transaction_enlistments.store(0, std::memory_order_release);
  transaction_commits.store(0, std::memory_order_release);
  transaction_rollbacks.store(0, std::memory_order_release);
}

observations query_counters() noexcept {
  observations result;
  result.last_scan_status =
      last_scan_status.load(std::memory_order_acquire);
  result.instances_registered =
      instances_registered.load(std::memory_order_acquire);
  result.policy_requests = policy_requests.load(std::memory_order_acquire);
  result.policy_failures = policy_failures.load(std::memory_order_acquire);
  result.open_scans = open_scans.load(std::memory_order_acquire);
  result.open_denied = open_denied.load(std::memory_order_acquire);
  result.write_scans = write_scans.load(std::memory_order_acquire);
  result.writes_allowed = writes_allowed.load(std::memory_order_acquire);
  result.writes_denied = writes_denied.load(std::memory_order_acquire);
  result.cleanup_scans = cleanup_scans.load(std::memory_order_acquire);
  result.cleanup_infected =
      cleanup_infected.load(std::memory_order_acquire);
  result.sections_created = sections_created.load(std::memory_order_acquire);
  result.sections_mapped = sections_mapped.load(std::memory_order_acquire);
  result.sections_closed = sections_closed.load(std::memory_order_acquire);
  result.section_conflicts =
      section_conflicts.load(std::memory_order_acquire);
  result.section_contexts_created =
      section_contexts_created.load(std::memory_order_acquire);
  result.section_contexts_destroyed =
      section_contexts_destroyed.load(std::memory_order_acquire);
  result.pended_writes = pended_writes.load(std::memory_order_acquire);
  result.resumed_writes = resumed_writes.load(std::memory_order_acquire);
  result.cancelled_writes =
      cancelled_writes.load(std::memory_order_acquire);
  result.deferred_writes = deferred_writes.load(std::memory_order_acquire);
  result.handle_contexts_created =
      handle_contexts_created.load(std::memory_order_acquire);
  result.handle_contexts_destroyed =
      handle_contexts_destroyed.load(std::memory_order_acquire);
  result.transaction_contexts_created =
      transaction_contexts_created.load(std::memory_order_acquire);
  result.transaction_contexts_destroyed =
      transaction_contexts_destroyed.load(std::memory_order_acquire);
  result.transaction_enlistments =
      transaction_enlistments.load(std::memory_order_acquire);
  result.transaction_commits =
      transaction_commits.load(std::memory_order_acquire);
  result.transaction_rollbacks =
      transaction_rollbacks.load(std::memory_order_acquire);
  return result;
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  reset_policy_counters();

  ntl::flt::communication_server messages;
  messages.contract(1)
      .register_client_method(scan_payload)
      .on(query_observations, []() noexcept { return query_counters(); })
      .on(reset_observations, []() noexcept {
        reset_policy_counters();
        return std::uint32_t{1};
      })
      .on_connect([](ntl::flt::communication_connection &connection) {
        std::lock_guard<std::mutex> guard(scanner_target_lock);
        if (scanner_target.connected())
          return ntl::status{STATUS_DEVICE_BUSY};
        scanner_target = connection;
        return ntl::status::ok();
      })
      .on_disconnect([](ntl::flt::communication_connection &connection) {
        std::lock_guard<std::mutex> guard(scanner_target_lock);
        if (scanner_target.id() == connection.id())
          scanner_target = {};
      });
  scanner_publisher = messages.publisher();

  ntl::flt::communication_port_options port_options;
  port_options.max_connections(1);
  const ntl::status port = driver.add_communication_port(
      port_name, std::move(messages), port_options);
  if (port.is_err())
    return port;

  ntl::flt::registration callbacks;
  callbacks
      .on_instance_setup([](ntl::flt::related_objects objects,
                            FLT_INSTANCE_SETUP_FLAGS, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE filesystem) noexcept {
        if (filesystem != FLT_FSTYPE_NTFS)
          return ntl::status{STATUS_FLT_DO_NOT_ATTACH};
        const ntl::status registered =
            ntl::flt::try_register_for_data_scan(objects.instance());
        if (registered.is_err())
          return registered;
        auto state = objects.try_get_or_create(
            scanner_instance_context, objects.instance().native_handle());
        if (!state)
          return state.status();
        if ((*state)->initialization.is_err())
          return (*state)->initialization;
        instances_registered.fetch_add(1, std::memory_order_release);
        return ntl::status::ok();
      })
      .on_instance_teardown_start(
          [](ntl::flt::related_objects objects,
             FLT_INSTANCE_TEARDOWN_FLAGS) noexcept {
            if (auto state = objects.try_get(scanner_instance_context))
              (*state)->pending_writes.shutdown(STATUS_CANCELLED);
          })
      .on(ntl::flt::operation::create, &pre_create, &post_create)
      .on(ntl::flt::operation::write, &pre_write,
          ntl::flt::operation_flags::skip_paging_io)
      .on(ntl::flt::operation::cleanup, &pre_cleanup)
      .on_section_notification(scanner_section_context, &section_conflict)
      .on_transaction_notification(scanner_transaction_context,
                                   &transaction_notification)
      .on_unload([](ntl::flt::unload_flags) noexcept {
        return ntl::status{STATUS_SUCCESS};
      })
      .context(scanner_instance_context)
      .context(scanner_handle_context);
  return driver.start(std::move(callbacks));
}
