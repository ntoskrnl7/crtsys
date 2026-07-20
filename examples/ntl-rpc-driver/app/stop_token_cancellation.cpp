#include <chrono>
#include <cstdio>
#include <stop_token>
#include <system_error>
#include <thread>

#include <ntl/rpc/coroutine>

#include "coroutine_task.hpp"
#include "examples.hpp"
#include "ntl_rpc_sample.hpp"

namespace crtsys_ntl_rpc_sample_app {
namespace {

coroutine_task<int> add_with_stop_token(std::stop_token token) {
  co_return co_await crtsys_ntl_rpc_sample::delayed_add_async(
      token, std::uint32_t{2000}, 40, 2);
}

} // namespace

void run_stop_token_cancellation() {
  using namespace std::chrono_literals;

  std::stop_source source;
  auto task = add_with_stop_token(source.get_token());
  std::this_thread::sleep_for(50ms);
  if (!source.request_stop())
    throw std::runtime_error("RPC stop request was not accepted");

  try {
    (void)task.get();
    throw std::runtime_error("stop-token RPC returned a result");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED)
      throw;
  }

  std::wprintf(L"stop_token: coroutine resumed with cancellation\n");
}

} // namespace crtsys_ntl_rpc_sample_app
