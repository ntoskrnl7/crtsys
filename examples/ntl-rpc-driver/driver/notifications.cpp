#include <ntddk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <ntl/rpc/server>

#include "notifications.hpp"
#include "operations.hpp"
#include "ntl_rpc_sample.hpp"

namespace {

struct session_state {
  std::atomic<std::uint64_t> published{0};
};

} // namespace

void configure_reliable_notifications(ntl::rpc::server &server) {
  auto *const endpoint = &server;
  server
      .register_notification(crtsys_ntl_rpc_sample::progress)
      .on_session_open(
          [](ntl::rpc::client_session &session,
             const ntl::rpc::call_context &) -> NTSTATUS {
            session.state(std::make_shared<session_state>());
            return STATUS_SUCCESS;
          })
      .on(crtsys_ntl_rpc_sample::publish_progress,
          [endpoint](const ntl::rpc::call_context &call,
                     std::uint64_t value) {
            auto *const session = call.session();
            if (!session)
              return false;
            auto state = session->state<session_state>();
            if (!state)
              return false;

            ntl_rpc_sample_progress progress;
            progress.value = value;
            progress.text = "reliable progress " + std::to_string(value);
            const auto status = endpoint->try_notify(
                session->id(), crtsys_ntl_rpc_sample::progress, progress);
            if (status.is_ok())
              state->published.fetch_add(1, std::memory_order_relaxed);
            return status.is_ok();
          });
}
