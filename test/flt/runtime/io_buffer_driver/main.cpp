#include <fltKernel.h>

#include <ntl/flt/all>
#include <ntl/irql>
#include <ntl/pool_allocator>

#include "io_buffer_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace {

using namespace crtsys_flt_io_buffer_runtime_test;

std::atomic<std::uint32_t> swapped_writes{0};
std::atomic<std::uint32_t> swapped_reads{0};
std::atomic<std::uint32_t> deferred_pre_operations{0};
std::atomic<std::uint32_t> deferred_post_operations{0};
std::atomic<std::uint32_t> fast_io_retries{0};
std::atomic<std::uint32_t> user_mapped_writes{0};
std::atomic<std::uint32_t> user_mapped_reads{0};
std::atomic<std::uint32_t> pending_pre_resumes{0};
std::atomic<std::uint32_t> pending_post_replies{0};
std::atomic<std::uint32_t> pending_pre_cancels{0};
std::atomic<std::uint32_t> pending_post_cancels{0};
std::atomic<std::uint32_t> active_pre_requests{0};
std::atomic<std::uint32_t> active_post_requests{0};

inline constexpr std::size_t max_pending_operations = 32;
inline constexpr ULONG deferred_write_pre_pool_tag = ntl::pool_tag("wPdN");
inline constexpr ULONG deferred_read_post_pool_tag = ntl::pool_tag("rPdN");

struct transform_test_gate {
  std::mutex configure_lock;
  std::atomic<std::uint32_t> configured{
      static_cast<std::uint32_t>(transform_test_behavior::normal)};
  std::atomic<std::uint32_t> waiters{0};
  std::atomic<std::uint64_t> mapped_address{0};
  KEVENT disconnect_event{};
  KEVENT teardown_event{};

  void initialize() noexcept {
    KeInitializeEvent(&disconnect_event, NotificationEvent, FALSE);
    KeInitializeEvent(&teardown_event, NotificationEvent, FALSE);
  }

  ntl::status configure(transform_test_behavior behavior) noexcept {
    std::lock_guard<std::mutex> guard(configure_lock);
    if (behavior != transform_test_behavior::normal &&
        behavior != transform_test_behavior::wait_for_disconnect &&
        behavior != transform_test_behavior::wait_for_teardown)
      return STATUS_INVALID_PARAMETER;
    if (waiters.load(std::memory_order_acquire) != 0)
      return STATUS_DEVICE_BUSY;
    if (behavior == transform_test_behavior::normal) {
      configured.store(static_cast<std::uint32_t>(behavior),
                       std::memory_order_release);
      return STATUS_SUCCESS;
    }
    if (configured.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(transform_test_behavior::normal))
      return STATUS_DEVICE_BUSY;
    if (behavior == transform_test_behavior::wait_for_disconnect)
      KeClearEvent(&disconnect_event);
    else
      KeClearEvent(&teardown_event);
    configured.store(static_cast<std::uint32_t>(behavior),
                     std::memory_order_release);
    return STATUS_SUCCESS;
  }

  transform_test_behavior wait_if_configured(std::uint64_t address) noexcept {
    const auto behavior =
        static_cast<transform_test_behavior>(configured.exchange(
            static_cast<std::uint32_t>(transform_test_behavior::normal),
            std::memory_order_acq_rel));
    if (behavior == transform_test_behavior::normal)
      return behavior;

    mapped_address.store(address, std::memory_order_release);
    waiters.fetch_add(1, std::memory_order_acq_rel);
    LARGE_INTEGER timeout{};
    timeout.QuadPart = -15ll * 10ll * 1000ll * 1000ll;
    const auto wait_status = KeWaitForSingleObject(
        behavior == transform_test_behavior::wait_for_disconnect
            ? &disconnect_event
            : &teardown_event,
        Executive, KernelMode, FALSE, &timeout);
    waiters.fetch_sub(1, std::memory_order_acq_rel);
    return NT_SUCCESS(wait_status) ? behavior : transform_test_behavior::normal;
  }

  void signal_disconnect() noexcept {
    KeSetEvent(&disconnect_event, IO_NO_INCREMENT, FALSE);
  }

  void signal_teardown() noexcept {
    KeSetEvent(&teardown_event, IO_NO_INCREMENT, FALSE);
  }
};

transform_test_gate pre_test_gate;
transform_test_gate post_test_gate;

struct io_buffer_instance_state {
  ntl::flt::pending_pre_operation_queue pending_pre;
  ntl::flt::pending_post_operation_registry pending_post;
  PFLT_INSTANCE instance = nullptr;
  ntl::status initialization;

  explicit io_buffer_instance_state(PFLT_INSTANCE instance) noexcept
      : instance(instance), initialization(pending_pre.initialize(
                                instance, max_pending_operations)) {
    if (initialization.is_ok())
      pending_post.reopen(max_pending_operations);
    else
      pending_post.shutdown(STATUS_CANCELLED);
  }
};

inline constexpr ntl::flt::instance_context<io_buffer_instance_state>
    io_buffer_instance_context{};

using io_buffer_instance_ref =
    ntl::flt::context_ref<io_buffer_instance_state,
                          ntl::flt::context_scope::instance>;

struct deferred_write_pre_context {
  io_buffer_instance_ref instance;

  explicit deferred_write_pre_context(io_buffer_instance_ref &&owner) noexcept
      : instance(std::move(owner)) {}
};

struct transform_target_snapshot {
  ntl::flt::communication_connection connection;
  ntl::ipc::process_connection process;
};

std::mutex transform_target_lock;
ntl::flt::communication_connection transform_target_connection;
ntl::flt::communication_publisher transform_publisher;

ntl::result<transform_target_snapshot> capture_transform_target() noexcept {
  try {
    std::lock_guard<std::mutex> guard(transform_target_lock);
    auto process = transform_target_connection.mapping_process();
    if (!transform_target_connection.connected() || !process.accepts_mappings())
      return ntl::unexpected(STATUS_DEVICE_NOT_CONNECTED);
    return ntl::ok(transform_target_snapshot{transform_target_connection,
                                             std::move(process)});
  } catch (...) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  }
}

struct pending_user_transform {
  ntl::flt::communication_connection connection;
  ntl::ipc::mapped_buffer_view mapping;
  ntl::ipc::mapped_buffer_descriptor descriptor{};
  transform_direction direction = transform_direction::write_input;
};

ntl::result<pending_user_transform>
begin_user_transform(const ntl::flt::swapped_buffer_view &buffer,
                     transform_direction direction,
                     std::size_t valid_bytes) noexcept {
  if (valid_bytes == 0 || valid_bytes > buffer.size())
    return ntl::unexpected(STATUS_INVALID_PARAMETER);
  auto target = capture_transform_target();
  if (!target)
    return ntl::unexpected(target.status());

  auto mapped =
      buffer.try_map(target->process, ntl::ipc::map_access::read_write);
  if (!mapped)
    return ntl::unexpected(mapped.status());

  ntl::ipc::mapped_buffer_descriptor descriptor{};
  if (valid_bytes == mapped->size()) {
    descriptor = mapped->descriptor();
  } else {
    auto logical_view = mapped->subview(0, valid_bytes);
    if (!logical_view) {
      const auto closed = mapped->close();
      return ntl::unexpected(closed.is_err() ? closed : logical_view.status());
    }
    descriptor = logical_view->descriptor();
  }
  if (!descriptor.valid()) {
    const auto closed = mapped->close();
    return ntl::unexpected(
        closed.is_err() ? closed : ntl::status{STATUS_INVALID_DEVICE_STATE});
  }

  return ntl::ok(pending_user_transform{target->connection, std::move(*mapped),
                                        descriptor, direction});
}

ntl::status finish_user_transform(pending_user_transform &request) noexcept {
  auto transformed = transform_publisher.try_request(
      request.connection, transform_mapped_buffer_method,
      std::chrono::milliseconds(transform_timeout_milliseconds),
      request.descriptor, request.direction);
  const auto closed = request.mapping.close();
  if (!transformed)
    return transformed.status();
  if (*transformed != request.descriptor.length)
    return ntl::status{STATUS_DATA_ERROR};
  return closed;
}

struct transform_attempt {
  ntl::status status;
  transform_test_behavior behavior = transform_test_behavior::normal;
};

transform_test_gate &test_gate_for(transform_direction direction) noexcept {
  return direction == transform_direction::write_input ? pre_test_gate
                                                       : post_test_gate;
}

transform_attempt
execute_user_transform(pending_user_transform &request) noexcept {
  const auto behavior =
      test_gate_for(request.direction)
          .wait_if_configured(request.descriptor.mapped_address);
  if (behavior == transform_test_behavior::normal)
    return {finish_user_transform(request), behavior};

  const auto closed = request.mapping.close();
  if (!closed.is_ok())
    return {closed, behavior};
  return {behavior == transform_test_behavior::wait_for_disconnect
              ? ntl::status{STATUS_DEVICE_NOT_CONNECTED}
              : ntl::status{STATUS_CANCELLED},
          behavior};
}

bool cancellation_completed(ntl::status transform_status,
                            ntl::status finish_status) noexcept {
  return !transform_status.is_ok() &&
         static_cast<NTSTATUS>(transform_status) ==
             static_cast<NTSTATUS>(finish_status);
}

runtime_state query_runtime_state() noexcept {
  runtime_state state{};
  state.active_pre_requests =
      active_pre_requests.load(std::memory_order_acquire);
  state.active_post_requests =
      active_post_requests.load(std::memory_order_acquire);
  state.pending_pre_resumes =
      pending_pre_resumes.load(std::memory_order_acquire);
  state.pending_post_replies =
      pending_post_replies.load(std::memory_order_acquire);
  state.pending_pre_cancels =
      pending_pre_cancels.load(std::memory_order_acquire);
  state.pending_post_cancels =
      pending_post_cancels.load(std::memory_order_acquire);
  state.waiting_pre_requests =
      pre_test_gate.waiters.load(std::memory_order_acquire);
  state.waiting_post_requests =
      post_test_gate.waiters.load(std::memory_order_acquire);
  state.waiting_pre_address =
      pre_test_gate.mapped_address.load(std::memory_order_acquire);
  state.waiting_post_address =
      post_test_gate.mapped_address.load(std::memory_order_acquire);
  return state;
}

template <class Data> bool targets_runtime_file(Data data) noexcept {
  if (!ntl::is_irql_at_most(ntl::irql::apc))
    return false;
  auto name = data.try_query_name(FLT_FILE_NAME_NORMALIZED |
                                  FLT_FILE_NAME_QUERY_DEFAULT);
  if (!name || name->try_parse().is_err())
    return false;
  return name->final_component() == target_file_name;
}

template <class Data>
ntl::flt::pre_result fail_pre(Data data, ntl::status status) noexcept {
  data.complete(status, 0);
  return ntl::flt::pre_result::complete;
}

NTSTATUS pend_write(ntl::flt::write_callback_data data,
                    io_buffer_instance_ref instance) noexcept {
  if (!targets_runtime_file(data))
    return STATUS_OBJECT_NAME_NOT_FOUND;

  auto swapped =
      ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(data));
  if (!swapped)
    return static_cast<NTSTATUS>(swapped.status());
  if (!swapped->input() || !swapped->input()->data())
    return STATUS_INVALID_DEVICE_STATE;

  auto request =
      begin_user_transform(*swapped->input(), transform_direction::write_input,
                           swapped->input()->size());
  if (!request)
    return static_cast<NTSTATUS>(request.status());

  auto key = instance->pending_pre.pend(ntl::flt::as_pre(data), std::nullopt,
                                        std::move(*swapped));
  if (!key) {
    const auto closed = request->mapping.close();
    return static_cast<NTSTATUS>(closed.is_err() ? closed : key.status());
  }
  active_pre_requests.fetch_add(1, std::memory_order_acq_rel);

  auto *const state = instance.get();
  auto worker_instance = instance.reference();
  auto work = [owner = std::move(worker_instance), request_key = *key,
               transform = std::move(*request)]() mutable noexcept {
    const auto transformed = execute_user_transform(transform);
    const auto finished =
        transformed.status.is_ok()
            ? owner->pending_pre.resume(request_key)
            : owner->pending_pre.cancel(
                  request_key, static_cast<NTSTATUS>(transformed.status));
    if (transformed.status.is_ok() && finished.is_ok()) {
      swapped_writes.fetch_add(1, std::memory_order_relaxed);
      user_mapped_writes.fetch_add(1, std::memory_order_relaxed);
      pending_pre_resumes.fetch_add(1, std::memory_order_relaxed);
    } else if (cancellation_completed(transformed.status, finished)) {
      pending_pre_cancels.fetch_add(1, std::memory_order_relaxed);
    }
    active_pre_requests.fetch_sub(1, std::memory_order_acq_rel);
  };
  const auto queued =
      ntl::flt::queue_instance_work_item(state->instance, std::move(work));
  if (!queued.is_ok()) {
    const auto cancelled =
        state->pending_pre.cancel(*key, static_cast<NTSTATUS>(queued));
    if (cancellation_completed(queued, cancelled))
      pending_pre_cancels.fetch_add(1, std::memory_order_relaxed);
    active_pre_requests.fetch_sub(1, std::memory_order_acq_rel);
  }

  // The pending queue owns the callback data once pend() succeeds. Returning
  // STATUS_PENDING remains mandatory even when detached work allocation
  // failed and cancel() already completed the request.
  return STATUS_PENDING;
}

ntl::flt::pre_result prepare_read(ntl::flt::read_callback_data data,
                                  void *&completion_context) noexcept {
  completion_context = nullptr;
  if (!targets_runtime_file(data))
    return ntl::flt::pre_result::success_no_callback;

  auto swapped =
      ntl::flt::try_swap_io_buffers(ntl::flt::as_pre(data));
  if (!swapped)
    return fail_pre(data, swapped.status());
  const auto applied = swapped->apply();
  if (!applied.is_ok()) {
    (void)swapped->complete();
    return fail_pre(data, applied);
  }
  auto released = swapped->release_completion_context();
  if (!released) {
    (void)swapped->complete();
    return fail_pre(data, released.status());
  }

  completion_context = *released;
  swapped_reads.fetch_add(1, std::memory_order_relaxed);
  return ntl::flt::pre_result::success_with_callback;
}

ntl::status
deferred_write_pre(PFLT_CALLBACK_DATA native, void *context,
                   ntl::flt::deferred_pre_completion &completion) noexcept {
  completion = {};
  auto *const holder = static_cast<deferred_write_pre_context *>(context);
  if (!holder)
    return STATUS_INVALID_PARAMETER;
  auto instance = std::move(holder->instance);
  ntl::pool_deleter<deferred_write_pre_context>{deferred_write_pre_pool_tag}(
      holder);
  if (!instance)
    return STATUS_INVALID_DEVICE_STATE;
  return pend_write(ntl::flt::write_callback_data{native}, std::move(instance));
}

ntl::status
deferred_read_pre(PFLT_CALLBACK_DATA native, void *,
                  ntl::flt::deferred_pre_completion &completion) noexcept {
  void *context = nullptr;
  completion.result =
      prepare_read(ntl::flt::read_callback_data{native}, context);
  completion.completion_context = context;
  return STATUS_SUCCESS;
}

template <class Data>
ntl::flt::pre_result
dispatch_pre_at_passive(Data data, void *&completion_context,
                        ntl::flt::deferred_pre_routine deferred,
                        void *deferred_context = nullptr) noexcept {
  completion_context = nullptr;
  if (data.is_fast_io_operation()) {
    if (ntl::is_irql_at_most(ntl::irql::apc) && !targets_runtime_file(data))
      return ntl::flt::pre_result::success_no_callback;
    fast_io_retries.fetch_add(1, std::memory_order_relaxed);
    return ntl::flt::pre_result::disallow_fast_io;
  }
  if (ntl::is_irql_at_most(ntl::irql::apc) && !targets_runtime_file(data))
    return ntl::flt::pre_result::success_no_callback;
  // This runtime fixture deliberately takes the deferred path even when the
  // callback already arrived at PASSIVE_LEVEL. A successful end-to-end I/O
  // therefore proves pending pre-operation ownership and completion instead
  // of merely compiling the helper's high-IRQL branch.
  const auto queued = ntl::flt::queue_pre_operation_at_passive(
      ntl::flt::as_pre(data), deferred, deferred_context);
  if (!queued.is_ok())
    return fail_pre(data, queued);
  deferred_pre_operations.fetch_add(1, std::memory_order_relaxed);
  return ntl::flt::pre_result::pending;
}

NTSTATUS finish_write(void *completion_context) noexcept {
  auto adopted = ntl::flt::swapped_io_buffers::adopt_completion_context(
      completion_context);
  if (!adopted)
    return static_cast<NTSTATUS>(adopted.status());
  return static_cast<NTSTATUS>(adopted->complete());
}

NTSTATUS pend_read_post(PFLT_CALLBACK_DATA native,
                        io_buffer_instance_ref instance,
                        void *completion_context) noexcept {
  auto adopted = ntl::flt::swapped_io_buffers::adopt_completion_context(
      completion_context);
  if (!adopted)
    return static_cast<NTSTATUS>(adopted.status());

  if (NT_ERROR(native->IoStatus.Status)) {
    const auto copied = native->IoStatus.Information == 0
                            ? adopted->copy_output_to_original(0)
                            : ntl::status{STATUS_SUCCESS};
    const auto completed = adopted->complete();
    if (!copied.is_ok())
      return static_cast<NTSTATUS>(copied);
    return static_cast<NTSTATUS>(completed);
  }

  const auto valid_bytes =
      (std::min)(adopted->output() ? adopted->output()->size() : std::size_t{0},
                 static_cast<std::size_t>(native->IoStatus.Information));
  if (!adopted->output() || !adopted->output()->data()) {
    (void)adopted->complete();
    return STATUS_INVALID_DEVICE_STATE;
  }
  const auto prepared = adopted->prepare_output_copy_back(valid_bytes);
  if (!prepared.is_ok()) {
    (void)adopted->complete();
    return static_cast<NTSTATUS>(prepared);
  }

  if (valid_bytes != 0) {
    auto request = begin_user_transform(
        *adopted->output(), transform_direction::read_output, valid_bytes);
    if (!request) {
      (void)adopted->complete();
      return static_cast<NTSTATUS>(request.status());
    }

    auto key = instance->pending_post.pend(
        ntl::flt::as_post(ntl::flt::read_callback_data{native}), std::nullopt,
        std::move(*adopted));
    if (!key) {
      const auto closed = request->mapping.close();
      return static_cast<NTSTATUS>(closed.is_err() ? closed : key.status());
    }
    active_post_requests.fetch_add(1, std::memory_order_acq_rel);

    auto *const state = instance.get();
    auto worker_instance = instance.reference();
    auto work = [owner = std::move(worker_instance), request_key = *key,
                 transform = std::move(*request)]() mutable noexcept {
      const auto transformed = execute_user_transform(transform);
      const auto finished =
          transformed.status.is_ok()
              ? owner->pending_post.reply(request_key)
              : owner->pending_post.cancel(
                    request_key, static_cast<NTSTATUS>(transformed.status));
      if (transformed.status.is_ok() && finished.is_ok()) {
        user_mapped_reads.fetch_add(1, std::memory_order_relaxed);
        pending_post_replies.fetch_add(1, std::memory_order_relaxed);
      } else if (cancellation_completed(transformed.status, finished)) {
        pending_post_cancels.fetch_add(1, std::memory_order_relaxed);
      }
      active_post_requests.fetch_sub(1, std::memory_order_acq_rel);
    };
    const auto queued =
        ntl::flt::queue_instance_work_item(state->instance, std::move(work));
    if (!queued.is_ok()) {
      const auto cancelled =
          state->pending_post.cancel(*key, static_cast<NTSTATUS>(queued));
      if (cancellation_completed(queued, cancelled))
        pending_post_cancels.fetch_add(1, std::memory_order_relaxed);
      active_post_requests.fetch_sub(1, std::memory_order_acq_rel);
    }
    return STATUS_PENDING;
  }

  const auto copied = adopted->copy_back(
      ntl::flt::as_post(ntl::flt::read_callback_data{native}));
  const auto completed = adopted->complete();
  if (!copied.is_ok())
    return static_cast<NTSTATUS>(copied);
  return static_cast<NTSTATUS>(completed);
}

struct deferred_read_post_context {
  io_buffer_instance_ref instance;
  void *completion_context = nullptr;

  deferred_read_post_context(io_buffer_instance_ref &&owner,
                             void *completion) noexcept
      : instance(std::move(owner)), completion_context(completion) {}
};

ntl::status deferred_read_post(PFLT_CALLBACK_DATA native,
                               void *opaque) noexcept {
  auto *const context = static_cast<deferred_read_post_context *>(opaque);
  if (!context)
    return STATUS_INVALID_PARAMETER;
  auto instance = std::move(context->instance);
  void *const completion_context = context->completion_context;
  ntl::pool_deleter<deferred_read_post_context>{deferred_read_post_pool_tag}(
      context);
  if (!instance)
    return STATUS_INVALID_PARAMETER;
  return pend_read_post(native, std::move(instance), completion_context);
}

ntl::flt::pre_result write_pre(ntl::flt::write_callback_data data,
                               ntl::flt::related_objects objects,
                               void *&completion_context) noexcept {
  auto instance = objects.try_get(io_buffer_instance_context);
  if (!instance)
    return fail_pre(data, instance.status());
  auto context = ntl::try_make_pool<deferred_write_pre_context>(
      ntl::pool_kind::nonpaged, ntl::pool_option::none,
      deferred_write_pre_pool_tag, std::move(*instance));
  if (!context)
    return fail_pre(data, context.status());
  auto *const raw_context = context->release();
  const auto result = dispatch_pre_at_passive(data, completion_context,
                                              &deferred_write_pre, raw_context);
  if (result != ntl::flt::pre_result::pending) {
    ntl::pool_deleter<deferred_write_pre_context>{deferred_write_pre_pool_tag}(
        raw_context);
  }
  return result;
}

ntl::flt::post_result
write_post(ntl::flt::write_callback_data data, ntl::flt::related_objects,
           void *completion_context,
           ntl::flt::post_operation_flags flags) noexcept {
  const NTSTATUS status = finish_write(completion_context);
  if (!flags.draining() && !NT_SUCCESS(status)) {
    data.native()->IoStatus.Status = status;
    data.native()->IoStatus.Information = 0;
  }
  return ntl::flt::post_result::finished;
}

ntl::flt::pre_result read_pre(ntl::flt::read_callback_data data,
                              ntl::flt::related_objects,
                              void *&completion_context) noexcept {
  return dispatch_pre_at_passive(data, completion_context, &deferred_read_pre);
}

ntl::flt::post_result read_post(ntl::flt::read_callback_data data,
                                ntl::flt::related_objects objects,
                                void *completion_context,
                                ntl::flt::post_operation_flags flags) noexcept {
  if (flags.draining()) {
    (void)finish_write(completion_context);
    return ntl::flt::post_result::finished;
  }
  auto instance = objects.try_get(io_buffer_instance_context);
  if (!instance) {
    (void)finish_write(completion_context);
    data.native()->IoStatus.Status = static_cast<NTSTATUS>(instance.status());
    data.native()->IoStatus.Information = 0;
    return ntl::flt::post_result::finished;
  }
  auto context = ntl::try_make_pool<deferred_read_post_context>(
      ntl::pool_kind::nonpaged, ntl::pool_option::none,
      deferred_read_post_pool_tag, std::move(*instance), completion_context);
  if (!context) {
    (void)finish_write(completion_context);
    data.native()->IoStatus.Status = static_cast<NTSTATUS>(context.status());
    data.native()->IoStatus.Information = 0;
    return ntl::flt::post_result::finished;
  }
  // As above, always pend the successful read post callback so the runtime
  // result covers the PASSIVE worker, copy-back, and pended-post completion.
  auto *const raw_context = context->release();
  const auto queued = ntl::flt::queue_post_operation_at_passive(
      ntl::flt::as_post(data), &deferred_read_post, raw_context);
  if (queued.is_ok()) {
    deferred_post_operations.fetch_add(1, std::memory_order_relaxed);
    return ntl::flt::post_result::more_processing_required;
  }

  ntl::pool_deleter<deferred_read_post_context>{deferred_read_post_pool_tag}(
      raw_context);
  (void)finish_write(completion_context);
  data.native()->IoStatus.Status = static_cast<NTSTATUS>(queued);
  data.native()->IoStatus.Information = 0;
  return ntl::flt::post_result::finished;
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  pre_test_gate.initialize();
  post_test_gate.initialize();

  ntl::flt::communication_server messages;
  messages.contract(1)
      .register_client_method(transform_mapped_buffer_method)
      .on(configure_transform_test_method,
          [](transform_direction direction,
             transform_test_behavior behavior) -> std::uint32_t {
            if (direction != transform_direction::write_input &&
                direction != transform_direction::read_output)
              throw ntl::exception(STATUS_INVALID_PARAMETER,
                                   "Invalid transform direction");
            const auto configured =
                test_gate_for(direction).configure(behavior);
            if (!configured.is_ok())
              throw ntl::exception(configured, "Transform test gate is busy");
            return 1;
          })
      .on(query_runtime_state_method,
          []() noexcept { return query_runtime_state(); })
      .on_connect([](ntl::flt::communication_connection &connection) {
        if (connection.process_id() != PsGetCurrentProcessId() ||
            !connection.mapping_process().accepts_mappings())
          return ntl::status{STATUS_ACCESS_DENIED};

        {
          std::lock_guard<std::mutex> guard(transform_target_lock);
          if (transform_target_connection.connected())
            return ntl::status{STATUS_DEVICE_BUSY};
          transform_target_connection = connection;
        }
        return ntl::status::ok();
      })
      .on_disconnect([](ntl::flt::communication_connection &connection) {
        pre_test_gate.signal_disconnect();
        post_test_gate.signal_disconnect();
        std::lock_guard<std::mutex> guard(transform_target_lock);
        if (transform_target_connection.id() == connection.id())
          transform_target_connection = {};
      });
  transform_publisher = messages.publisher();

  ntl::flt::communication_port_options port_options;
  ntl::ipc::process_connection_limits mapping_limits;
  mapping_limits.max_mappings = 4;
  mapping_limits.max_mapped_bytes = payload_bytes * 4;
  port_options.max_connections(1).process_mapping_limits(mapping_limits);
  const auto port_status = driver.add_communication_port(
      port_name, std::move(messages), port_options);
  if (!port_status.is_ok())
    return port_status;

  ntl::flt::registration callbacks;
  callbacks
      .on_instance_setup([](ntl::flt::related_objects objects,
                            FLT_INSTANCE_SETUP_FLAGS, DEVICE_TYPE,
                            FLT_FILESYSTEM_TYPE) noexcept {
        auto state = objects.try_get_or_create(
            io_buffer_instance_context, objects.instance().native_handle());
        return state ? (*state)->initialization : state.status();
      })
      .on_instance_teardown_start([](ntl::flt::related_objects objects,
                                     FLT_INSTANCE_TEARDOWN_FLAGS) noexcept {
        pre_test_gate.signal_teardown();
        post_test_gate.signal_teardown();
        if (auto state = objects.try_get(io_buffer_instance_context)) {
          (*state)->pending_pre.shutdown(STATUS_CANCELLED);
          (*state)->pending_post.shutdown(STATUS_CANCELLED);
        }
      })
      .on(ntl::flt::operation::write, &write_pre, &write_post,
          ntl::flt::operation_flags::skip_paging_io)
      .on(ntl::flt::operation::read, &read_pre, &read_post,
          ntl::flt::operation_flags::skip_paging_io)
      .on_unload([](ntl::flt::unload_flags) noexcept {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[crtsys FLT I/O buffer runtime] writes=%lu reads=%lu "
                   "deferred-pre=%lu deferred-post=%lu fastio-retries=%lu "
                   "user-mapped-writes=%lu user-mapped-reads=%lu "
                   "pending-pre-resumes=%lu pending-post-replies=%lu "
                   "pending-pre-cancels=%lu pending-post-cancels=%lu "
                   "active-pre=%lu active-post=%lu\n",
                   swapped_writes.load(std::memory_order_relaxed),
                   swapped_reads.load(std::memory_order_relaxed),
                   deferred_pre_operations.load(std::memory_order_relaxed),
                   deferred_post_operations.load(std::memory_order_relaxed),
                   fast_io_retries.load(std::memory_order_relaxed),
                   user_mapped_writes.load(std::memory_order_relaxed),
                   user_mapped_reads.load(std::memory_order_relaxed),
                   pending_pre_resumes.load(std::memory_order_relaxed),
                   pending_post_replies.load(std::memory_order_relaxed),
                   pending_pre_cancels.load(std::memory_order_relaxed),
                   pending_post_cancels.load(std::memory_order_relaxed),
                   active_pre_requests.load(std::memory_order_relaxed),
                   active_post_requests.load(std::memory_order_relaxed));
        return ntl::status{STATUS_SUCCESS};
      })
      .context(io_buffer_instance_context);
  return driver.start(std::move(callbacks));
}
