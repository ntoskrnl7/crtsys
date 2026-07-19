#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <stdexcept>

#include <ntl/rpc/client>

#include "ntl_rpc_sample.hpp"
#include "examples.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_asynchronous_call() {
  using namespace std::chrono_literals;

  auto call = crtsys_ntl_rpc_sample::delayed_add_async(
      std::uint32_t{100}, 40, 2);

  // The request owns its OVERLAPPED state and buffers, so this thread can do
  // other work before waiting for and consuming the typed result.
  std::wprintf(L"async: request started; caller remains available\n");
  if (call.wait_for(2s) != ntl::rpc::async_wait_status::completed) {
    throw std::runtime_error("asynchronous RPC timed out unexpectedly");
  }

  const int sum = call.get();
  if (sum != 42) {
    throw std::runtime_error("unexpected asynchronous add result");
  }
  std::wprintf(L"async: completed with add=%d\n", sum);
}

} // namespace crtsys_ntl_rpc_sample_app
