#include <ntddk.h>

#include <atomic>
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
      .max_pending_notifications(16);

  auto server = ntl::rpc::make_server(driver, options);
  auto *const endpoint = server.get();
  server
      ->register_notification(crtsys_rpc_notifications::progress)
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
            state->dropped.load(std::memory_order_relaxed)};
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
