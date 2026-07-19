#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <ntl/rpc/client>

#include "ntl_rpc_sample.hpp"
#include "examples.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_cancellation() {
  using namespace std::chrono_literals;

  auto call = crtsys_ntl_rpc_sample::delayed_add_async(
      std::uint32_t{2000}, 40, 2);
  if (call.wait_for(50ms) != ntl::rpc::async_wait_status::timeout) {
    throw std::runtime_error("cancellation example completed unexpectedly");
  }

  if (!call.cancel()) {
    throw std::runtime_error("cancellation request was not accepted");
  }

  try {
    (void)call.get();
    throw std::runtime_error("cancelled RPC returned a result");
  } catch (const std::system_error &error) {
    if (error.code().value() != ERROR_OPERATION_ABORTED) {
      throw;
    }
  }

  std::wprintf(L"cancel: kernel callback observed cancellation\n");
}

} // namespace crtsys_ntl_rpc_sample_app
