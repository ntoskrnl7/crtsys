#include <ntddk.h>

#include <cstdint>
#include <memory>

#include <ntl/driver>
#include <ntl/rpc/server>

#include "caller_security.hpp"
#include "ntl_rpc_caller_security.hpp"

std::shared_ptr<ntl::rpc::server>
start_caller_security_server(ntl::driver &driver) {
  ntl::rpc::server_options options(L"crtsys_ntl_rpc_security_sample");
  options.asynchronous();

  auto server = ntl::rpc::make_server(driver, options);
  server
      ->on(crtsys_ntl_rpc_security::caller_info,
           [](const ntl::rpc::call_context &caller) {
             return ntl_rpc_caller_info{
                 static_cast<std::uint64_t>(reinterpret_cast<ULONG_PTR>(
                     caller.requestor_process_id())),
                 caller.is_user_mode() ? 1u : 0u};
           })
      .on_authorized(
          crtsys_ntl_rpc_security::user_mode_echo,
          [](const ntl::rpc::call_context &caller) -> NTSTATUS {
            return caller.is_user_mode() ? STATUS_SUCCESS
                                         : STATUS_ACCESS_DENIED;
          },
          [](std::uint32_t value) { return value; })
      .start();
  return server;
}
