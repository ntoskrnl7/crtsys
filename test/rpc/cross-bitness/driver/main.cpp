#include <ntddk.h>

#include <atomic>
#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>
#include <ntl/system_thread>

#include "rpc_cross_bitness.hpp"

namespace {

struct rpc_test_state {
  std::weak_ptr<ntl::rpc::server> server;
  ntl::system_thread stop_thread;
  std::atomic<std::uint32_t> guarded_calls{0};
  std::atomic<std::uint32_t> concurrent_calls{0};
  std::atomic<std::uint32_t> slow_calls{0};
  std::atomic<bool> stop_requested{false};
};

void NTAPI stop_server_thread(void *context) {
  auto *const state = static_cast<rpc_test_state *>(context);
  if (auto server = state->server.lock())
    server->stop();
  PsTerminateSystemThread(STATUS_SUCCESS);
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto echo_count = std::make_shared<std::atomic<std::uint32_t>>(0);
  auto state = std::make_shared<rpc_test_state>();
  ntl::rpc::server_options options(L"crtsys_rpc_cross_bitness");
  options.contract_version(crtsys_rpc_cross_bitness::contract_version)
      .capabilities(crtsys_rpc_cross_bitness::capabilities::current);
  auto server = ntl::rpc::make_server(driver, options);
  state->server = server;
  server
      ->on(crtsys_rpc_cross_bitness::architecture, [] {
        return rpc_architecture_info{
            static_cast<std::uint32_t>(sizeof(void *) * 8),
            static_cast<std::uint32_t>(sizeof(std::size_t) * 8),
            static_cast<std::uint32_t>(sizeof(std::ptrdiff_t) * 8),
            static_cast<std::uint32_t>(sizeof(wchar_t) * 8)};
      })
      .on(crtsys_rpc_cross_bitness::echo,
          [echo_count](const rpc_cross_bitness_payload &payload) {
            echo_count->fetch_add(1, std::memory_order_relaxed);
            return payload;
          })
      .on(crtsys_rpc_cross_bitness::narrow_reply,
          [] { return std::uint32_t{0x89ABCDEFu}; })
      .on(crtsys_rpc_cross_bitness::vector_size,
          [](const std::vector<std::uint8_t> &values) {
            return static_cast<std::uint32_t>(values.size());
          })
      .on(crtsys_rpc_cross_bitness::echo_count, [echo_count] {
        return echo_count->load(std::memory_order_relaxed);
      })
      .on(crtsys_rpc_cross_bitness::guarded_words,
          [state](const std::vector<std::string> &words) {
            state->guarded_calls.fetch_add(1, std::memory_order_relaxed);
            return static_cast<std::uint32_t>(words.size());
          })
      .on(crtsys_rpc_cross_bitness::guarded_count, [state] {
        return state->guarded_calls.load(std::memory_order_relaxed);
      })
      .on(crtsys_rpc_cross_bitness::concurrent_increment,
          [state](std::uint32_t amount) {
            return state->concurrent_calls.fetch_add(
                       amount, std::memory_order_relaxed) +
                   amount;
          })
      .on(crtsys_rpc_cross_bitness::concurrent_count, [state] {
        return state->concurrent_calls.load(std::memory_order_relaxed);
      })
      .on(crtsys_rpc_cross_bitness::slow_call,
          [state](std::uint32_t milliseconds) {
            state->slow_calls.fetch_add(1, std::memory_order_relaxed);
            LARGE_INTEGER interval{};
            interval.QuadPart =
                -static_cast<LONGLONG>(milliseconds) * 10'000;
            (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
            state->slow_calls.fetch_sub(1, std::memory_order_relaxed);
            return milliseconds;
          })
      .on(crtsys_rpc_cross_bitness::slow_active, [state] {
        return state->slow_calls.load(std::memory_order_relaxed);
      })
      .on(crtsys_rpc_cross_bitness::request_stop, [state] {
        bool expected = false;
        if (!state->stop_requested.compare_exchange_strong(expected, true))
          return std::uint32_t{0};

        auto thread = ntl::system_thread::create(stop_server_thread,
                                                 state.get());
        if (!thread) {
          state->stop_requested.store(false);
          throw ntl::exception(thread.status(),
                               "Could not start RPC stop thread");
        }
        state->stop_thread = std::move(thread).value();
        return std::uint32_t{1};
      });
  server->start();
  constexpr auto late_method = ntl::rpc::method<0xAFE, void()>{};
  try {
    server->on(late_method, [] {});
    return ntl::status{STATUS_INVALID_DEVICE_STATE};
  } catch (const ntl::exception &error) {
    if (error.get_status() != STATUS_INVALID_DEVICE_STATE)
      return ntl::status{error.get_status()};
  }

  driver.on_unload([server, echo_count, state]() mutable {
    if (state->stop_thread)
      (void)state->stop_thread.join();
    server.reset();
    echo_count.reset();
    state.reset();
  });
  return ntl::status::ok();
}
