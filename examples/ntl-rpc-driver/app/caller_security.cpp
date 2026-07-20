#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include <ntl/rpc/client>

#include "examples.hpp"
#include "ntl_rpc_caller_security.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_caller_security() {
  ntl::rpc::client client(L"crtsys_ntl_rpc_security_sample");
  const auto caller = client.invoke(crtsys_ntl_rpc_security::caller_info);
  const auto echoed =
      client.invoke(crtsys_ntl_rpc_security::user_mode_echo, 42u);

  if (!caller.user_mode ||
      caller.process_id != static_cast<std::uint64_t>(GetCurrentProcessId()) ||
      echoed != 42u) {
    throw std::runtime_error("unexpected RPC caller security result");
  }

  std::printf("security: caller_pid=%llu mode=user authorized_echo=%u\n",
              static_cast<unsigned long long>(caller.process_id), echoed);
}

} // namespace crtsys_ntl_rpc_sample_app
