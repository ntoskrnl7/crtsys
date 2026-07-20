#include <ntddk.h>

#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "caller_security.hpp"
#include "notifications.hpp"
#include "operations.hpp"
#include "ntl_rpc_sample.hpp"

ntl::status ntl::main(ntl::driver &driver,
                      const std::wstring &registry_path) {
  (void)registry_path;

  ntl::rpc::server_options options(L"crtsys_ntl_rpc_sample");
  options.asynchronous().max_pending_calls(16);
  auto rpc_server = crtsys_ntl_rpc_sample::make_server(driver, options);
  configure_reliable_notifications(*rpc_server);
  rpc_server->start();
  auto security_server = start_caller_security_server(driver);

  driver.on_unload([rpc_server, security_server]() mutable {
    security_server.reset();
    rpc_server.reset();
  });

  return ntl::status::ok();
}
