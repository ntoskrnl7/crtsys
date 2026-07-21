#include <ntddk.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ntl/driver>
#include <ntl/irql>
#include <ntl/rpc/server>
#include <ntl/status>

#include "rpc_notifications.hpp"

namespace {

struct notification_state {
  std::atomic<std::uint32_t> delivered{0};
  std::atomic<std::uint32_t> dropped{0};
  std::atomic<std::uint32_t> sessions_opened{0};
  std::atomic<std::uint32_t> sessions_resumed{0};
  std::atomic<std::uint32_t> sessions_disconnected{0};
  std::atomic<std::uint32_t> sessions_closed{0};
  std::atomic<std::uint32_t> persisted{0};
  std::atomic<std::uint32_t> acknowledged{0};
};

class counting_notification_store final
    : public ntl::rpc::notification_store {
public:
  explicit counting_notification_store(
      std::shared_ptr<notification_state> state) noexcept
      : state_(std::move(state)) {
    ExInitializeFastMutex(&lock_);
  }

  ntl::status persist(
      const ntl::rpc::notification_record_view &view) noexcept override {
    if (!view.session.valid() || view.notification_id == 0 ||
        view.sequence == 0 || !view.data || view.size == 0)
      return ntl::status{STATUS_INVALID_PARAMETER};
    try {
      stored_record record;
      record.token = view.session;
      record.notification_id = view.notification_id;
      record.sequence = view.sequence;
      record.terminal = view.terminal;
      record.bytes.assign(view.data, view.data + view.size);
      lock_guard guard(lock_);
      records_.push_back(std::move(record));
      state_->persisted.fetch_add(1, std::memory_order_relaxed);
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    }
  }

  ntl::status acknowledge(const ntl::rpc::session_token &token,
                          std::uint32_t notification_id,
                          std::uint64_t sequence) noexcept override {
    lock_guard guard(lock_);
    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
                       [&](const stored_record &record) {
                         return record.token == token &&
                                record.notification_id == notification_id &&
                                record.sequence == sequence;
                       }),
        records_.end());
    state_->acknowledged.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status restore(const ntl::rpc::session_token &token,
                      ntl::rpc::notification_restore_sink &sink) noexcept
      override {
    lock_guard guard(lock_);
    bool found = false;
    for (const auto &record : records_) {
      if (record.token != token)
        continue;
      found = true;
      const auto status = sink.add(record.notification_id, record.sequence,
                                   record.bytes.data(), record.bytes.size(),
                                   record.terminal);
      if (!status.is_ok())
        return status;
    }
    return found ? ntl::status::ok() : ntl::status{STATUS_NOT_FOUND};
  }

  void erase_session(const ntl::rpc::session_token &token) noexcept override {
    lock_guard guard(lock_);
    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
                       [&](const stored_record &record) {
                         return record.token == token;
                       }),
        records_.end());
  }

private:
  class lock_guard {
  public:
    explicit lock_guard(FAST_MUTEX &lock) noexcept : lock_(&lock) {
      ExAcquireFastMutex(lock_);
    }
    ~lock_guard() { ExReleaseFastMutex(lock_); }

  private:
    FAST_MUTEX *lock_;
  };

  struct stored_record {
    ntl::rpc::session_token token{};
    std::uint32_t notification_id = 0;
    std::uint64_t sequence = 0;
    bool terminal = false;
    std::vector<unsigned char> bytes;
  };

  std::shared_ptr<notification_state> state_;
  FAST_MUTEX lock_{};
  std::vector<stored_record> records_;
};

rpc_notification_message make_message(std::uint64_t sequence,
                                      std::uint32_t value_count) {
  rpc_notification_message message;
  message.sequence = sequence;
  message.text = "notification-" + std::to_string(sequence);
  message.values.reserve(value_count);
  for (std::uint32_t index = 0; index != value_count; ++index)
    message.values.push_back(static_cast<std::uint32_t>(sequence) + index);
  return message;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<notification_state>();
  ntl::rpc::server_options options(L"crtsys_rpc_notifications");
  options.contract_version(crtsys_rpc_notifications::contract_version)
      .capabilities(crtsys_rpc_notifications::capabilities::current)
      .asynchronous()
      .max_pending_calls(32)
      .max_pending_notifications(16)
      .max_reliable_notifications_per_session(4)
      .max_reliable_notifications(32)
      .session_retention_ms(500);

  auto server = ntl::rpc::make_server(driver, options);
  auto *const endpoint = server.get();
  auto store = std::make_shared<counting_notification_store>(state);
  server
      ->notification_storage(store)
      .on_session_open(
          [state](ntl::rpc::client_session &session,
                   const ntl::rpc::call_context &) -> NTSTATUS {
            if (KeGetCurrentIrql() != PASSIVE_LEVEL)
              return STATUS_INVALID_DEVICE_STATE;
            session.state(std::make_shared<std::uint64_t>(session.id()));
            state->sessions_opened.fetch_add(1, std::memory_order_relaxed);
            return STATUS_SUCCESS;
          })
      .on_session_resume(
          [state](ntl::rpc::client_session &session,
                  const ntl::rpc::call_context &) -> NTSTATUS {
            const auto value = session.state<std::uint64_t>();
            if (value && *value != session.id())
              return STATUS_INVALID_DEVICE_STATE;
            if (!value)
              session.state(std::make_shared<std::uint64_t>(session.id()));
            state->sessions_resumed.fetch_add(1, std::memory_order_relaxed);
            return STATUS_SUCCESS;
          })
      .on_session_disconnect([state](ntl::rpc::client_session &) {
        state->sessions_disconnected.fetch_add(1,
                                               std::memory_order_relaxed);
      })
      .on_session_close([state](ntl::rpc::client_session &) {
        state->sessions_closed.fetch_add(1, std::memory_order_relaxed);
      })
      .register_notification(crtsys_rpc_notifications::progress)
      .on(crtsys_rpc_notifications::publish,
          [endpoint, state](std::uint64_t sequence,
                            std::uint32_t value_count) {
            const auto status = endpoint->try_notify(
                crtsys_rpc_notifications::progress,
                make_message(sequence, value_count));
            (status.is_ok() ? state->delivered : state->dropped)
                .fetch_add(1, std::memory_order_relaxed);
            return status.is_ok();
          })
      .on(crtsys_rpc_notifications::publish_at_dispatch,
          [endpoint, state](std::uint64_t sequence) {
            auto message = make_message(sequence, 1);
            ntl::raised_irql raised(ntl::to_dpc_level);
            const auto status = endpoint->try_notify(
                crtsys_rpc_notifications::progress, message);
            if (!status.is_ok())
              state->dropped.fetch_add(1, std::memory_order_relaxed);
            return static_cast<std::int32_t>(static_cast<NTSTATUS>(status));
          })
      .on(crtsys_rpc_notifications::query_stats, [state] {
        return rpc_notification_stats{
            state->delivered.load(std::memory_order_relaxed),
            state->dropped.load(std::memory_order_relaxed),
            state->sessions_opened.load(std::memory_order_relaxed),
            state->sessions_resumed.load(std::memory_order_relaxed),
            state->sessions_disconnected.load(std::memory_order_relaxed),
            state->sessions_closed.load(std::memory_order_relaxed),
            state->persisted.load(std::memory_order_relaxed),
            state->acknowledged.load(std::memory_order_relaxed)};
      })
      .on(crtsys_rpc_notifications::publish_reliable,
          [endpoint](const ntl::rpc::call_context &context,
                     std::uint64_t sequence, std::uint32_t value_count) {
            auto *const session = context.session();
            if (!session)
              return static_cast<std::int32_t>(STATUS_INVALID_HANDLE);
            const auto status = endpoint->try_notify(
                session->id(), crtsys_rpc_notifications::progress,
                make_message(sequence, value_count));
            return static_cast<std::int32_t>(static_cast<NTSTATUS>(status));
          })
      .on(crtsys_rpc_notifications::hold_session,
          [](const ntl::rpc::call_context &context,
             std::uint32_t milliseconds) {
            auto *const session = context.session();
            if (!session)
              return std::uint64_t{0};
            LARGE_INTEGER interval{};
            interval.QuadPart =
                -static_cast<LONGLONG>(milliseconds) * 10 * 1000;
            (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
            return session->id();
          });
  server->start();

  driver.on_unload([server, state]() mutable {
    server.reset();
    DbgPrint("[crtsys RPC notifications] delivered=%lu dropped=%lu\n",
             state->delivered.load(std::memory_order_relaxed),
             state->dropped.load(std::memory_order_relaxed));
    state.reset();
  });
  return ntl::status::ok();
}
