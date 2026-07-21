#include <ntddk.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "rpc_streaming.hpp"
#include "rpc_streaming_macros.hpp"

namespace {

struct stream_state {
  std::atomic<std::uint32_t> uploads{0};
  std::atomic<std::uint32_t> queued{0};
  std::atomic<std::uint32_t> backpressured{0};
  std::atomic<std::uint32_t> cancellations{0};
};

class reverse_notification_store final : public ntl::rpc::notification_store {
public:
  reverse_notification_store() noexcept { ExInitializeFastMutex(&lock_); }

  ntl::status
  persist(const ntl::rpc::notification_record_view &view) noexcept override {
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
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    }
  }

  ntl::status acknowledge(const ntl::rpc::session_token &token,
                          std::uint32_t notification_id,
                          std::uint64_t sequence) noexcept override {
    lock_guard guard(lock_);
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [&](const stored_record &record) {
                                    return record.token == token &&
                                           record.notification_id ==
                                               notification_id &&
                                           record.sequence == sequence;
                                  }),
                   records_.end());
    return ntl::status::ok();
  }

  ntl::status
  restore(const ntl::rpc::session_token &token,
          ntl::rpc::notification_restore_sink &sink) noexcept override {
    lock_guard guard(lock_);
    bool found = false;
    // Return records in reverse order on purpose. The RPC layer owns ordering
    // and must reconstruct the original session sequence before publication.
    for (auto iterator = records_.rbegin(); iterator != records_.rend();
         ++iterator) {
      if (iterator->token != token)
        continue;
      found = true;
      const auto status = sink.add(iterator->notification_id,
                                   iterator->sequence, iterator->bytes.data(),
                                   iterator->bytes.size(), iterator->terminal);
      if (!status.is_ok())
        return status;
    }
    return found ? ntl::status::ok() : ntl::status{STATUS_NOT_FOUND};
  }

  void erase_session(const ntl::rpc::session_token &token) noexcept override {
    lock_guard guard(lock_);
    records_.erase(std::remove_if(records_.begin(), records_.end(),
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

  FAST_MUTEX lock_{};
  std::vector<stored_record> records_;
};

rpc_stream_download make_download(const rpc_stream_upload &upload,
                                  std::uint32_t index) {
  rpc_stream_download value;
  value.sequence = upload.sequence + index;
  value.text = upload.text + "-" + std::to_string(index);
  value.values = upload.values;
  value.values.push_back(index);
  return value;
}

void cancellable_delay(const ntl::rpc::call_context &context,
                       stream_state &state, std::uint32_t milliseconds) {
  constexpr std::uint32_t quantum_ms = 5;
  for (std::uint32_t elapsed = 0; elapsed < milliseconds;) {
    if (context.cancelled())
      break;
    const auto remaining = milliseconds - elapsed;
    const auto delay = (std::min)(remaining, quantum_ms);
    LARGE_INTEGER interval{};
    interval.QuadPart = -static_cast<LONGLONG>(delay) * 10 * 1000;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
    elapsed += delay;
  }
  if (context.cancelled())
    state.cancellations.fetch_add(1, std::memory_order_relaxed);
  context.throw_if_cancelled();
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<stream_state>();
  auto store = std::make_shared<reverse_notification_store>();
  ntl::rpc::server_options options(L"crtsys_rpc_streaming");
  options.contract_version(crtsys_rpc_streaming::contract_version)
      .capabilities(crtsys_rpc_streaming::capabilities::current)
      .asynchronous()
      .max_pending_calls(8)
      .max_pending_notifications(8)
      .max_reliable_notifications_per_session(2)
      .max_reliable_notifications(16)
      .session_retention_ms(200);

  auto server = ntl::rpc::make_server(driver, options);
  std::weak_ptr<ntl::rpc::server> weak_server = server;
  server->notification_storage(store)
      .register_notification(crtsys_rpc_streaming::background_events)
      .register_notification(crtsys_rpc_streaming::critical_events)
      .on_stream(
          crtsys_rpc_streaming::records,
          [weak_server, state](const ntl::rpc::call_context &context,
                               const rpc_stream_upload &upload) {
            auto endpoint = weak_server.lock();
            auto *const session = context.session();
            if (!endpoint || !session)
              throw ntl::exception(STATUS_DELETE_PENDING,
                                   "stream endpoint is unavailable");

            state->uploads.fetch_add(1, std::memory_order_relaxed);
            if (upload.action == rpc_stream_action::delayed_echo)
              cancellable_delay(context, *state, upload.delay_ms);

            if (upload.action == rpc_stream_action::publish_priorities) {
              auto status = endpoint->try_notify(
                  session->id(), crtsys_rpc_streaming::background_events,
                  rpc_priority_event{1});
              if (!status.is_ok())
                throw ntl::exception(status, "background event was not queued");
              status = endpoint->try_notify(
                  session->id(), crtsys_rpc_streaming::critical_events,
                  rpc_priority_event{2});
              if (!status.is_ok())
                throw ntl::exception(status, "critical event was not queued");
              return;
            }

            if (upload.action == rpc_stream_action::complete) {
              const auto status = endpoint->try_complete(
                  session->id(), crtsys_rpc_streaming::records);
              if (!status.is_ok())
                throw ntl::exception(status,
                                     "stream completion was not queued");
              state->queued.fetch_add(1, std::memory_order_relaxed);
              return;
            }

            if (upload.action == rpc_stream_action::fail) {
              const auto status = endpoint->try_fail(
                  session->id(), crtsys_rpc_streaming::records,
                  STATUS_IO_DEVICE_ERROR);
              if (!status.is_ok())
                throw ntl::exception(status, "stream failure was not queued");
              state->queued.fetch_add(1, std::memory_order_relaxed);
              return;
            }

            const auto count =
                upload.action == rpc_stream_action::burst ? upload.count : 1u;
            for (std::uint32_t index = 0; index != count; ++index) {
              context.throw_if_cancelled();
              const auto status = endpoint->try_write(
                  session->id(), crtsys_rpc_streaming::records,
                  make_download(upload, index));
              if (status.is_ok()) {
                state->queued.fetch_add(1, std::memory_order_relaxed);
              } else if (static_cast<NTSTATUS>(status) == STATUS_DEVICE_BUSY) {
                state->backpressured.fetch_add(1, std::memory_order_relaxed);
              } else {
                throw ntl::exception(status, "stream chunk was not queued");
              }
            }
          })
      .on(crtsys_rpc_streaming::query_stats, [state] {
        return rpc_stream_stats{
            state->uploads.load(std::memory_order_relaxed),
            state->queued.load(std::memory_order_relaxed),
            state->backpressured.load(std::memory_order_relaxed),
            state->cancellations.load(std::memory_order_relaxed)};
      });
  server->start();

  driver.on_unload([server, state]() mutable {
    server.reset();
    state.reset();
  });
  return ntl::status::ok();
}
