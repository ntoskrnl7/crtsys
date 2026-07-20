#include <cstdio>
#include <stdexcept>

#include <ntl/rpc/coroutine>

#include "coroutine_task.hpp"
#include "examples.hpp"
#include "ntl_rpc_sample.hpp"

namespace crtsys_ntl_rpc_sample_app {
namespace {

coroutine_task<int> add_with_coroutine() {
  co_return co_await crtsys_ntl_rpc_sample::delayed_add_async(
      std::uint32_t{100}, 40, 2);
}

} // namespace

void run_coroutine_call() {
  auto task = add_with_coroutine();
  std::wprintf(L"coroutine: suspended without blocking the caller\n");

  const int sum = task.get();
  if (sum != 42)
    throw std::runtime_error("unexpected coroutine RPC result");

  std::wprintf(L"coroutine: resumed with add=%d\n", sum);
}

} // namespace crtsys_ntl_rpc_sample_app
