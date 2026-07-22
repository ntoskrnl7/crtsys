#include "communication_advanced.hpp"

#include "../shared/runtime_test.hpp"

#include <ntl/flt/driver>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace crtsys_flt_runtime_test {
namespace {

struct connection_test_state {
  explicit connection_test_state(std::uint64_t value) noexcept
      : connection_id(value) {}

  std::uint64_t connection_id = 0;
  std::atomic<std::uint32_t> calls{0};
};

class test_notification_store final
    : public ntl::flt::communication_notification_store {
public:
  ntl::status
  persist(const ntl::flt::communication_record_view &) noexcept override {
    persisted_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status acknowledge(const ntl::rpc::session_token &, std::uint32_t,
                          std::uint64_t) noexcept override {
    acknowledged_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status acknowledge_batch(const ntl::rpc::session_token &, std::uint32_t,
                                const std::uint64_t *sequences,
                                std::size_t count) noexcept override {
    if (!sequences || count == 0)
      return ntl::status{STATUS_INVALID_PARAMETER};
    acknowledged_.fetch_add(static_cast<std::uint32_t>(count),
                            std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status
  restore(const ntl::rpc::session_token &token,
          ntl::flt::communication_restore_sink &sink) noexcept override {
    if (token != restored_session_token)
      return ntl::status{STATUS_NOT_FOUND};

    try {
      progress_event event{0x52455354};
      std::vector<unsigned char> payload(
          decltype(progress_notification)::response_size());
      zpp::serializer::memory_view_output_archive archive(payload.data(),
                                                          payload.size());
      archive(event);
      payload.resize(archive.offset());
      const auto result =
          sink.add(decltype(progress_notification)::id(), 1,
                   ntl::flt::communication_record_kind::notification,
                   payload.data(), payload.size());
      if (result.is_ok())
        restored_.fetch_add(1, std::memory_order_relaxed);
      return result;
    } catch (const std::bad_alloc &) {
      return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    } catch (...) {
      return ntl::status{STATUS_UNSUCCESSFUL};
    }
  }

  std::uint64_t stats() const noexcept {
    return (static_cast<std::uint64_t>(
                persisted_.load(std::memory_order_relaxed))
            << 42) |
           (static_cast<std::uint64_t>(
                acknowledged_.load(std::memory_order_relaxed))
            << 21) |
           restored_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint32_t> persisted_{0};
  std::atomic<std::uint32_t> acknowledged_{0};
  std::atomic<std::uint32_t> restored_{0};
};

std::atomic<std::uint32_t> connected_count{0};
std::atomic<std::uint32_t> disconnected_count{0};

} // namespace

void configure_advanced_communication_tests(
    ntl::flt::communication_server &server,
    const ntl::flt::communication_publisher &publisher) {
  auto store = std::make_shared<test_notification_store>();
  server
      .on_connect([](ntl::flt::communication_connection &connection) {
        connection.state(
            std::make_shared<connection_test_state>(connection.id()));
        connected_count.fetch_add(1, std::memory_order_relaxed);
        return ntl::status::ok();
      })
      .on_disconnect([](ntl::flt::communication_connection &connection) {
        if (connection.state<connection_test_state>())
          disconnected_count.fetch_add(1, std::memory_order_relaxed);
      })
      .notification_storage(store)
      .register_client_method(client_transform_method)
      .on(request_client_method,
          [publisher](const ntl::flt::communication_context &context,
                      std::uint32_t value) -> std::uint32_t {
            auto response = publisher.try_request(
                context.connection(), client_transform_method,
                std::chrono::seconds(2), value);
            if (!response)
              throw ntl::exception(response.status(),
                                   "User-mode request failed");
            return *response;
          })
      .on(publish_targeted_method,
          [publisher](const ntl::flt::communication_context &context,
                      std::uint32_t value) -> std::uint64_t {
            const auto status = publisher.try_notify(context.connection(),
                                                     progress_notification,
                                                     progress_event{value});
            if (status.is_err())
              throw ntl::exception(status, "Targeted notification failed");
            return context.connection().id();
          })
      .on(connection_state_method,
          [](const ntl::flt::communication_context &context) -> std::uint64_t {
            auto state = context.connection().state<connection_test_state>();
            if (!state)
              throw ntl::exception(STATUS_INVALID_DEVICE_STATE,
                                   "Connection state is unavailable");
            const auto calls =
                state->calls.fetch_add(1, std::memory_order_relaxed) + 1;
            return (state->connection_id << 32) | calls;
          })
      .on(storage_stats_method, [store]() noexcept { return store->stats(); })
      .on(connection_lifecycle_stats_method, []() noexcept {
        return (static_cast<std::uint64_t>(
                    connected_count.load(std::memory_order_relaxed))
                << 32) |
               disconnected_count.load(std::memory_order_relaxed);
      });
}

ntl::status add_drop_policy_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  const auto publisher = server.publisher();
  server.register_notification(background_progress_notification)
      .on(drop_publish_method,
          [publisher](std::uint64_t session_id,
                      std::uint32_t value) -> std::uint64_t {
            auto sequence = publisher.try_notify(
                session_id, background_progress_notification,
                progress_event{value});
            if (!sequence)
              throw ntl::exception(sequence.status(),
                                   "Drop-policy notification failed");
            return *sequence;
          });

  ntl::flt::communication_port_options options;
  options.max_reliable_records(2).reliable_overflow(
      ntl::flt::communication_overflow_policy::drop_oldest);
  return driver.add_communication_port(drop_port_name, std::move(server),
                                       options);
}

ntl::status add_reject_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  server.on_connect([](ntl::flt::communication_connection &) noexcept {
    return ntl::status{STATUS_ACCESS_DENIED};
  });
  ntl::flt::communication_port_options options;
  options.max_connections(2);
  return driver.add_communication_port(reject_port_name, std::move(server),
                                       options);
}

ntl::status add_byte_quota_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  const auto publisher = server.publisher();
  server.register_notification(background_progress_notification)
      .on(drop_publish_method,
          [publisher](std::uint64_t session_id,
                      std::uint32_t value) -> std::uint64_t {
            auto sequence = publisher.try_notify(
                session_id, background_progress_notification,
                progress_event{value});
            if (!sequence)
              throw ntl::exception(sequence.status(),
                                   "Byte-quota notification failed");
            return *sequence;
          });

  ntl::flt::communication_port_options options;
  options.max_reliable_records(8).max_reliable_bytes(8);
  return driver.add_communication_port(byte_quota_port_name, std::move(server),
                                       options);
}

ntl::status add_connection_limit_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  ntl::flt::communication_port_options options;
  options.max_connections(1);
  return driver.add_communication_port(connection_limit_port_name,
                                       std::move(server), options);
}

ntl::status add_session_limit_test_port(ntl::flt::driver &driver) {
  ntl::flt::communication_server server;
  ntl::flt::communication_port_options options;
  options.max_connections(2).max_sessions(1);
  return driver.add_communication_port(session_limit_port_name,
                                       std::move(server), options);
}

} // namespace crtsys_flt_runtime_test
