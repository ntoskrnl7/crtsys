#include "ownership_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <ntl/except>
#include <ntl/flt/callback_data_owner>
#include <ntl/flt/data_scan>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>

namespace crtsys_flt_runtime_test {
namespace {

std::atomic<std::uint32_t> transaction_contexts_created{0};
std::atomic<std::uint32_t> transaction_enlistments{0};
std::atomic<std::uint32_t> transaction_commits{0};
std::atomic<std::uint32_t> transaction_rollbacks{0};
std::atomic<std::uint32_t> transaction_contexts_destroyed{0};
std::atomic<NTSTATUS> data_scan_last_registration_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> data_scan_last_create_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> data_scan_last_map_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> data_scan_instances_registered{0};
std::atomic<std::uint32_t> data_scan_create_candidates{0};
std::atomic<std::uint32_t> data_scan_sections_created{0};
std::atomic<std::uint32_t> data_scan_sections_mapped{0};
std::atomic<std::uint32_t> data_scan_conflicts{0};
std::atomic<std::uint32_t> data_scan_sections_closed{0};
std::atomic<std::uint32_t> data_scan_contexts_destroyed{0};
std::atomic<long> data_scan_armed{0};
std::atomic<NTSTATUS> self_io_last_sync_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> self_io_last_async_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> self_io_last_cancel_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> self_io_sync_issued{0};
std::atomic<std::uint32_t> self_io_sync_completed{0};
std::atomic<std::uint32_t> self_io_async_issued{0};
std::atomic<std::uint32_t> self_io_async_completed{0};
std::atomic<std::uint32_t> self_io_cancellable_issued{0};
std::atomic<std::uint32_t> self_io_cancel_requests{0};
std::atomic<std::uint32_t> self_io_cancel_requests_accepted{0};
std::atomic<std::uint32_t> self_io_cancellation_completed{0};
std::atomic<std::uint32_t> self_io_handles_released{0};
std::atomic<long> self_io_armed{0};
FILE_STANDARD_INFORMATION self_io_async_information{};
alignas(void *) std::array<std::byte, 1024> self_io_notify_buffer{};
std::mutex self_io_mutex;
std::optional<ntl::flt::async_callback_data_operation> self_io_operation;

void complete_self_io_query(ntl::flt::callback_data_owner data,
                            FILE_STANDARD_INFORMATION *information) noexcept {
  const ntl::status status = data.view().io_status();
  self_io_last_async_status.store(status, std::memory_order_release);
  if (status.is_ok() && information && information->Directory) {
    self_io_async_completed.fetch_add(1, std::memory_order_release);
  }
}

void complete_self_io_cancel(ntl::flt::callback_data_owner data) noexcept {
  const ntl::status status = data.view().io_status();
  self_io_last_cancel_status.store(status, std::memory_order_release);
  if (status == STATUS_CANCELLED) {
    self_io_cancellation_completed.fetch_add(1, std::memory_order_release);
  }
}

bool cancel_self_io() noexcept {
  ntl::flt::async_callback_data_operation operation;
  {
    std::lock_guard<std::mutex> guard(self_io_mutex);
    if (!self_io_operation)
      return false;
    operation = *self_io_operation;
  }
  self_io_cancel_requests.fetch_add(1, std::memory_order_release);
  const bool accepted = operation.cancel();
  if (accepted) {
    self_io_cancel_requests_accepted.fetch_add(1, std::memory_order_release);
  }
  return accepted;
}

ntl::status release_self_io() noexcept {
  std::optional<ntl::flt::async_callback_data_operation> operation;
  {
    std::lock_guard<std::mutex> guard(self_io_mutex);
    operation = std::move(self_io_operation);
    self_io_operation.reset();
  }
  if (!operation)
    return STATUS_NOT_FOUND;

  if (!operation->completed())
    (void)operation->cancel();
  const ntl::status status = operation->wait();
  operation.reset();
  if (status.is_ok()) {
    self_io_handles_released.fetch_add(1, std::memory_order_release);
  }
  return status;
}

#if FLT_MGR_LONGHORN
struct transaction_state {
  transaction_state() noexcept {
    transaction_contexts_created.fetch_add(1, std::memory_order_relaxed);
  }

  ~transaction_state() noexcept {
    transaction_contexts_destroyed.fetch_add(1, std::memory_order_relaxed);
  }
};

inline constexpr ntl::flt::transaction_context<transaction_state>
    transaction_state_context{};

ntl::status transaction_notification(
    ntl::flt::related_objects, ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  if (!state)
    return STATUS_OBJECT_TYPE_MISMATCH;

  if (ntl::flt::any(notifications &
                    ntl::flt::transaction_notifications::commit_finalize)) {
    transaction_commits.fetch_add(1, std::memory_order_relaxed);
  }
  if (ntl::flt::any(notifications &
                    ntl::flt::transaction_notifications::rollback)) {
    transaction_rollbacks.fetch_add(1, std::memory_order_relaxed);
  }
  return STATUS_SUCCESS;
}
#endif

#if FLT_MGR_WIN8
struct section_state {
  section_state() noexcept = default;

  ~section_state() noexcept {
    data_scan_contexts_destroyed.fetch_add(1, std::memory_order_relaxed);
  }
};

inline constexpr ntl::flt::section_context<section_state>
    section_state_context{};

struct held_data_scan_section {
  ntl::flt::context_ref<section_state, ntl::flt::context_scope::section>
      context;
  HANDLE handle = nullptr;
  PVOID object = nullptr;
  HANDLE process = nullptr;
  PVOID view = nullptr;
};

std::mutex held_data_scan_mutex;
std::optional<held_data_scan_section> held_data_scan;

ntl::status close_held_data_scan() noexcept;

ntl::status section_conflict(ntl::flt::instance target_instance,
                             ntl::flt::context_view<section_state> state,
                             ntl::flt::callback_data_view data) noexcept {
  if (!target_instance || !state || !data)
    return STATUS_INVALID_PARAMETER;
  data_scan_conflicts.fetch_add(1, std::memory_order_release);
  (void)close_held_data_scan();
  return STATUS_SUCCESS;
}

ntl::status close_held_data_scan() noexcept {
  std::optional<held_data_scan_section> section;
  {
    std::lock_guard<std::mutex> guard(held_data_scan_mutex);
    section = std::move(held_data_scan);
    held_data_scan.reset();
  }
  if (!section)
    return STATUS_NOT_FOUND;

  NTSTATUS first_error = STATUS_SUCCESS;
  if (section->view && section->process) {
    const NTSTATUS status =
        ZwUnmapViewOfSection(section->process, section->view);
    if (!NT_SUCCESS(status))
      first_error = status;
  }
  if (section->process) {
    const NTSTATUS status = ZwClose(section->process);
    if (!NT_SUCCESS(status) && NT_SUCCESS(first_error))
      first_error = status;
  }
  if (section->handle) {
    const NTSTATUS status = ZwClose(section->handle);
    if (!NT_SUCCESS(status))
      first_error = status;
  }
  if (section->object)
    ObDereferenceObject(section->object);

  const ntl::status close_status =
      ntl::flt::close_data_scan_section(section->context);
  if (close_status.is_err() && NT_SUCCESS(first_error))
    first_error = close_status;
  if (NT_SUCCESS(first_error))
    data_scan_sections_closed.fetch_add(1, std::memory_order_release);
  return first_error;
}
#endif

} // namespace

void configure_ownership_runtime_messages(
    ntl::flt::communication_server &messages) {
  messages.on(transaction_observations_method, []() noexcept {
    transaction_observations result;
    result.contexts_created =
        transaction_contexts_created.load(std::memory_order_acquire);
    result.enlistments =
        transaction_enlistments.load(std::memory_order_acquire);
    result.commits = transaction_commits.load(std::memory_order_acquire);
    result.rollbacks = transaction_rollbacks.load(std::memory_order_acquire);
    result.contexts_destroyed =
        transaction_contexts_destroyed.load(std::memory_order_acquire);
    return result;
  });
#if FLT_MGR_WIN8
  messages.on(data_scan_observations_method, []() noexcept {
    data_scan_observations result;
    result.last_registration_status =
        data_scan_last_registration_status.load(std::memory_order_acquire);
    result.last_create_status =
        data_scan_last_create_status.load(std::memory_order_acquire);
    result.last_map_status =
        data_scan_last_map_status.load(std::memory_order_acquire);
    result.instances_registered =
        data_scan_instances_registered.load(std::memory_order_acquire);
    result.create_candidates =
        data_scan_create_candidates.load(std::memory_order_acquire);
    result.sections_created =
        data_scan_sections_created.load(std::memory_order_acquire);
    result.sections_mapped =
        data_scan_sections_mapped.load(std::memory_order_acquire);
    result.conflicts = data_scan_conflicts.load(std::memory_order_acquire);
    result.sections_closed =
        data_scan_sections_closed.load(std::memory_order_acquire);
    result.contexts_destroyed =
        data_scan_contexts_destroyed.load(std::memory_order_acquire);
    return result;
  });
  messages.on(close_data_scan_method, []() noexcept {
    return static_cast<std::int32_t>(close_held_data_scan());
  });
  messages.on(arm_data_scan_method, []() noexcept {
    data_scan_armed.store(1, std::memory_order_release);
    return std::uint32_t{1};
  });
#else
  messages.on(data_scan_observations_method,
              []() noexcept { return data_scan_observations{}; });
  messages.on(close_data_scan_method, []() noexcept {
    return static_cast<std::int32_t>(STATUS_NOT_SUPPORTED);
  });
  messages.on(arm_data_scan_method, []() noexcept { return std::uint32_t{0}; });
#endif
  messages.on(self_issued_io_observations_method, []() noexcept {
    self_issued_io_observations result;
    result.last_sync_status =
        self_io_last_sync_status.load(std::memory_order_acquire);
    result.last_async_status =
        self_io_last_async_status.load(std::memory_order_acquire);
    result.last_cancel_status =
        self_io_last_cancel_status.load(std::memory_order_acquire);
    result.sync_issued = self_io_sync_issued.load(std::memory_order_acquire);
    result.sync_completed =
        self_io_sync_completed.load(std::memory_order_acquire);
    result.async_issued = self_io_async_issued.load(std::memory_order_acquire);
    result.async_completed =
        self_io_async_completed.load(std::memory_order_acquire);
    result.cancellable_issued =
        self_io_cancellable_issued.load(std::memory_order_acquire);
    result.cancel_requests =
        self_io_cancel_requests.load(std::memory_order_acquire);
    result.cancel_requests_accepted =
        self_io_cancel_requests_accepted.load(std::memory_order_acquire);
    result.cancellation_completed =
        self_io_cancellation_completed.load(std::memory_order_acquire);
    result.handles_released =
        self_io_handles_released.load(std::memory_order_acquire);
    return result;
  });
  messages.on(arm_self_issued_io_method, []() noexcept {
    self_io_armed.store(1, std::memory_order_release);
    return std::uint32_t{1};
  });
  messages.on(cancel_self_issued_io_method, []() noexcept {
    return cancel_self_io() ? std::uint32_t{1} : std::uint32_t{0};
  });
  messages.on(release_self_issued_io_method, []() noexcept {
    return static_cast<std::int32_t>(release_self_io());
  });
}

void configure_ownership_runtime_registration(
    ntl::flt::registration &callbacks) {
#if FLT_MGR_LONGHORN
  callbacks.on_transaction_notification(transaction_state_context,
                                        &transaction_notification);
#endif
#if FLT_MGR_WIN8
  callbacks.on_section_notification(section_state_context, &section_conflict);
#endif
#if !FLT_MGR_LONGHORN && !FLT_MGR_WIN8
  (void)callbacks;
#endif
}

ntl::status
prepare_ownership_instance(ntl::flt::related_objects objects) noexcept {
#if FLT_MGR_WIN8
  const ntl::status status =
      ntl::flt::try_register_for_data_scan(objects.instance());
  data_scan_last_registration_status.store(status, std::memory_order_release);
  if (status.is_ok()) {
    data_scan_instances_registered.fetch_add(1, std::memory_order_release);
  }
  return status;
#else
  (void)objects;
  return STATUS_NOT_SUPPORTED;
#endif
}

void observe_transaction_create(ntl::flt::create_callback_data data,
                                ntl::flt::related_objects objects) noexcept {
#if FLT_MGR_LONGHORN
  if (data.io_status().is_err() || !objects.transaction())
    return;

  auto state = objects.try_get_or_create(transaction_state_context);
  if (!state)
    return;

  const ntl::status status = objects.try_enlist(
      *state, ntl::flt::transaction_notifications::commit_finalize |
                  ntl::flt::transaction_notifications::rollback);
  if (status.is_ok()) {
    transaction_enlistments.fetch_add(1, std::memory_order_release);
  }
#else
  (void)data;
  (void)objects;
#endif
}

void observe_data_scan_create(ntl::flt::create_callback_data data,
                              ntl::flt::related_objects objects) noexcept {
#if FLT_MGR_WIN8
  if (data.io_status().is_err() || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !objects.file() ||
      data_scan_armed.exchange(0, std::memory_order_acq_rel) == 0) {
    return;
  }

  data_scan_create_candidates.fetch_add(1, std::memory_order_release);

  {
    std::lock_guard<std::mutex> guard(held_data_scan_mutex);
    if (held_data_scan)
      return;
  }

  auto context = ntl::flt::try_allocate_section_context(
      objects.filter().native_handle(), section_state_context);
  if (!context) {
    data_scan_last_create_status.store(context.status(),
                                       std::memory_order_release);
    return;
  }

  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE, nullptr,
                             nullptr);
  ntl::flt::data_scan_section_options options;
  options.object_attributes = &attributes;
  auto section = ntl::flt::try_create_data_scan_section(
      objects.instance(), objects.file(), *context, options);
  if (!section) {
    data_scan_last_create_status.store(section.status(),
                                       std::memory_order_release);
    return;
  }
  data_scan_last_create_status.store(STATUS_SUCCESS, std::memory_order_release);
  data_scan_sections_created.fetch_add(1, std::memory_order_release);

  PVOID view = nullptr;
  SIZE_T view_bytes = 0;
  HANDLE process = nullptr;
  OBJECT_ATTRIBUTES process_attributes{};
  InitializeObjectAttributes(&process_attributes, nullptr, OBJ_KERNEL_HANDLE,
                             nullptr, nullptr);
  CLIENT_ID client_id{};
  client_id.UniqueProcess = PsGetCurrentProcessId();
  constexpr ACCESS_MASK process_vm_operation = 0x0008;
  NTSTATUS map_status = ZwOpenProcess(&process, process_vm_operation,
                                      &process_attributes, &client_id);
  if (NT_SUCCESS(map_status)) {
    map_status =
        ZwMapViewOfSection(section->handle, process, &view, 0, 0, nullptr,
                           &view_bytes, ViewUnmap, 0, PAGE_READONLY);
  }
  data_scan_last_map_status.store(map_status, std::memory_order_release);
  if (NT_SUCCESS(map_status) && view && view_bytes != 0) {
    volatile unsigned char first_byte = 0;
    const auto guarded = ntl::seh::try_except(
        [&] { first_byte = *static_cast<volatile unsigned char *>(view); });
    (void)first_byte;
    if (std::get<0>(guarded)) {
      data_scan_sections_mapped.fetch_add(1, std::memory_order_release);
    }
  }

  bool retained = false;
  {
    std::lock_guard<std::mutex> guard(held_data_scan_mutex);
    if (!held_data_scan) {
      held_data_scan.emplace(
          held_data_scan_section{std::move(*context), section->handle,
                                 section->object, process, view});
      retained = true;
    }
  }
  if (!retained) {
    if (view && process)
      (void)ZwUnmapViewOfSection(process, view);
    if (process)
      (void)ZwClose(process);
    (void)ZwClose(section->handle);
    ObDereferenceObject(section->object);
    (void)ntl::flt::close_data_scan_section(*context);
  }
#else
  (void)data;
  (void)objects;
#endif
}

void observe_self_issued_io_create(ntl::flt::create_callback_data data,
                                   ntl::flt::related_objects objects) noexcept {
  if (data.io_status().is_err() || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !objects.file() ||
      self_io_armed.exchange(0, std::memory_order_acq_rel) == 0) {
    return;
  }

  FILE_STANDARD_INFORMATION synchronous_information{};
  auto synchronous =
      ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
  if (!synchronous) {
    self_io_last_sync_status.store(synchronous.status(),
                                   std::memory_order_release);
    return;
  }
  auto synchronous_query =
      synchronous->prepare(ntl::flt::operation::query_information);
  synchronous_query.parameters().length(sizeof(synchronous_information));
  synchronous_query.parameters().information_class(FileStandardInformation);
  synchronous_query.parameters().buffer(&synchronous_information);
  self_io_sync_issued.fetch_add(1, std::memory_order_release);
  const ntl::status synchronous_status = synchronous->perform_synchronously();
  self_io_last_sync_status.store(synchronous_status, std::memory_order_release);
  if (synchronous_status.is_ok() && synchronous_information.Directory) {
    self_io_sync_completed.fetch_add(1, std::memory_order_release);
  }

  RtlZeroMemory(&self_io_async_information, sizeof(self_io_async_information));
  auto asynchronous =
      ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
  if (!asynchronous) {
    self_io_last_async_status.store(asynchronous.status(),
                                    std::memory_order_release);
    return;
  }
  auto asynchronous_query =
      asynchronous->prepare(ntl::flt::operation::query_information);
  asynchronous_query.parameters().length(sizeof(self_io_async_information));
  asynchronous_query.parameters().information_class(FileStandardInformation);
  asynchronous_query.parameters().buffer(&self_io_async_information);
  self_io_async_issued.fetch_add(1, std::memory_order_release);
  auto asynchronous_operation =
      asynchronous->try_perform_asynchronously<complete_self_io_query>(
          &self_io_async_information);
  if (!asynchronous_operation) {
    self_io_last_async_status.store(asynchronous_operation.status(),
                                    std::memory_order_release);
    return;
  }

  auto cancellable =
      ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
  if (!cancellable) {
    self_io_last_cancel_status.store(cancellable.status(),
                                     std::memory_order_release);
    return;
  }
  auto notification = cancellable->prepare(
      ntl::flt::operation::directory_control, IRP_MN_NOTIFY_CHANGE_DIRECTORY);
  notification.parameters().notification(
      self_io_notify_buffer.data(),
      static_cast<ULONG>(self_io_notify_buffer.size()),
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
          FILE_NOTIFY_CHANGE_LAST_WRITE);
  auto cancellable_operation =
      cancellable->try_perform_asynchronously<complete_self_io_cancel>();
  if (!cancellable_operation) {
    self_io_last_cancel_status.store(cancellable_operation.status(),
                                     std::memory_order_release);
    return;
  }
  {
    std::lock_guard<std::mutex> guard(self_io_mutex);
    self_io_operation = std::move(*cancellable_operation);
  }
  self_io_cancellable_issued.fetch_add(1, std::memory_order_release);
}

void close_ownership_runtime() noexcept {
  (void)release_self_io();
#if FLT_MGR_WIN8
  (void)close_held_data_scan();
#endif
}

} // namespace crtsys_flt_runtime_test
