#include <ntddk.h>

#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "operations.hpp"
#include "ntl_rpc_sample.hpp"

ntl::status ntl::main(ntl::driver &driver,
                      const std::wstring &registry_path) {
  (void)registry_path;

  ntl::rpc::server_options options(L"crtsys_ntl_rpc_sample");
  options.asynchronous().max_pending_calls(16);
  auto rpc_server = crtsys_ntl_rpc_sample::init(driver, options);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset();
  });

  return ntl::status::ok();
}
