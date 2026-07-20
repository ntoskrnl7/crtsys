#pragma once

#include <memory>

namespace ntl {
class driver;
namespace rpc {
class server;
} // namespace rpc
} // namespace ntl

std::shared_ptr<ntl::rpc::server>
start_caller_security_server(ntl::driver &driver);
