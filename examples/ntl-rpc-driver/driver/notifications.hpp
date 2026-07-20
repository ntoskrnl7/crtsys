#pragma once

namespace ntl {
namespace rpc {
class server;
} // namespace rpc
} // namespace ntl

void configure_reliable_notifications(ntl::rpc::server &server);
