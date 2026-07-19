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

#include "rpc_async.hpp"

namespace {

struct async_state {
  std::atomic<std::uint32_t> started{0};
  std::atomic<std::uint32_t> completed{0};
  std::atomic<std::uint32_t> cancellations_observed{0};
};

void delay(std::uint32_t milliseconds) noexcept {
  LARGE_INTEGER interval{};
  interval.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
  (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

void cooperative_delay(ntl::rpc::call_context context, async_state &state,
                       std::uint32_t milliseconds) {
  constexpr std::uint32_t quantum_ms = 10;
  std::uint32_t elapsed = 0;
  while (elapsed < milliseconds) {
    if (context.cancelled()) {
      state.cancellations_observed.fetch_add(1, std::memory_order_relaxed);
      context.throw_if_cancelled();
    }
    const auto remaining = milliseconds - elapsed;
    const auto interval = remaining < quantum_ms ? remaining : quantum_ms;
    delay(interval);
    elapsed += interval;
  }
  if (context.cancelled()) {
    state.cancellations_observed.fetch_add(1, std::memory_order_relaxed);
    context.throw_if_cancelled();
  }
}

class call_guard {
public:
  explicit call_guard(async_state &state) noexcept : state_(&state) {
    state_->started.fetch_add(1, std::memory_order_relaxed);
  }
  call_guard(const call_guard &) = delete;
  call_guard &operator=(const call_guard &) = delete;
  ~call_guard() {
    state_->completed.fetch_add(1, std::memory_order_release);
  }

private:
  async_state *state_;
};

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<async_state>();
  ntl::rpc::server_options options(L"crtsys_rpc_async");
  options.contract_version(crtsys_rpc_async::contract_version)
      .capabilities(crtsys_rpc_async::capabilities::current)
      .asynchronous()
      .max_pending_calls(32);

  auto server = ntl::rpc::make_server(driver, options);
  server
      ->on(crtsys_rpc_async::delayed_value,
          [state](std::uint32_t milliseconds, std::uint32_t value) {
            call_guard guard(*state);
            delay(milliseconds);
            return value;
          })
      .on(crtsys_rpc_async::delayed_void,
          [state](std::uint32_t milliseconds) {
            call_guard guard(*state);
            delay(milliseconds);
          })
      .on(crtsys_rpc_async::checksum,
          [state](const std::vector<std::uint32_t> &values) {
            call_guard guard(*state);
            return std::accumulate(values.begin(), values.end(),
                                   std::uint64_t{0});
          })
      .on(crtsys_rpc_async::cooperative_value,
          [state](ntl::rpc::call_context context, std::uint32_t milliseconds,
                  std::uint32_t value) {
            call_guard guard(*state);
            cooperative_delay(context, *state, milliseconds);
            return value;
          })
      .on(crtsys_rpc_async::query_stats, [state] {
        return rpc_async_stats{
            state->started.load(std::memory_order_relaxed),
            state->completed.load(std::memory_order_acquire),
            state->cancellations_observed.load(std::memory_order_acquire)};
      });
  server->start();

  driver.on_unload([server, state]() mutable {
    server.reset();
    DbgPrint("[crtsys RPC async] completed %lu/%lu calls\n",
             state->completed.load(std::memory_order_relaxed),
             state->started.load(std::memory_order_relaxed));
    state.reset();
  });
  return ntl::status::ok();
}
