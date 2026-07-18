#include <ntddk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "rpc_lifecycle_stress.hpp"

namespace {

struct lifecycle_state {
  std::atomic<std::uint64_t> completed_calls{0};
  std::atomic<std::uint32_t> active_calls{0};
};

class active_call_guard {
public:
  explicit active_call_guard(lifecycle_state &state) noexcept : state_(&state) {
    state_->active_calls.fetch_add(1, std::memory_order_acq_rel);
  }

  active_call_guard(const active_call_guard &) = delete;
  active_call_guard &operator=(const active_call_guard &) = delete;

  ~active_call_guard() {
    state_->active_calls.fetch_sub(1, std::memory_order_acq_rel);
    state_->completed_calls.fetch_add(1, std::memory_order_relaxed);
  }

private:
  lifecycle_state *state_;
};

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<lifecycle_state>();
  ntl::rpc::server_options options(L"crtsys_rpc_lifecycle_stress");
  options.contract_version(crtsys_rpc_lifecycle_stress::contract_version)
      .capabilities(crtsys_rpc_lifecycle_stress::capabilities::current);

  auto server = ntl::rpc::make_server(driver, options);
  server
      ->on(crtsys_rpc_lifecycle_stress::ping,
          [state](std::uint64_t value) {
            active_call_guard active(*state);
            return value ^ crtsys_rpc_lifecycle_stress::ping_mask;
          })
      .on(crtsys_rpc_lifecycle_stress::checksum,
          [state](const std::vector<std::uint32_t> &values) {
            active_call_guard active(*state);
            return std::accumulate(values.begin(), values.end(),
                                   std::uint64_t{0});
          })
      .on(crtsys_rpc_lifecycle_stress::slow_call,
          [state](std::uint32_t milliseconds) {
            active_call_guard active(*state);
            LARGE_INTEGER interval{};
            interval.QuadPart =
                -static_cast<LONGLONG>(milliseconds) * 10'000;
            (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
            return milliseconds;
          })
      .on(crtsys_rpc_lifecycle_stress::query_stats, [state] {
        return rpc_lifecycle_stats{
            state->completed_calls.load(std::memory_order_relaxed),
            state->active_calls.load(std::memory_order_acquire)};
      });
  server->start();

  driver.on_unload([server, state]() mutable {
    server.reset();
    DbgPrint("[crtsys RPC lifecycle] unloaded after %llu calls\n",
             state->completed_calls.load(std::memory_order_relaxed));
    state.reset();
  });
  return ntl::status::ok();
}
