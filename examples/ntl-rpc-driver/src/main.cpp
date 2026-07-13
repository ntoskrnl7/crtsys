#include <ntddk.h>

#include <memory>
#include <string>

#include <ntl/driver>
#include <ntl/rpc/server>
#include <ntl/status>

#include "ntl_rpc_sample.hpp"

ntl::status ntl::main(ntl::driver &driver,
                      const std::wstring &registry_path) {
  (void)registry_path;

  auto rpc_server = crtsys_ntl_rpc_sample::init(driver);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset();
  });

  return ntl::status::ok();
}
