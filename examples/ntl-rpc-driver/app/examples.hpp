#pragma once

#include <cstdint>

namespace ntl {
namespace rpc {
class client;
} // namespace rpc
} // namespace ntl

namespace crtsys_ntl_rpc_sample_app {

void run_synchronous_calls(ntl::rpc::client &client, std::uint32_t value,
                           std::uint32_t bias);

void run_caller_security();

void run_asynchronous_call();

void run_cancellation();

void run_coroutine_call();

void run_stop_token_cancellation();

} // namespace crtsys_ntl_rpc_sample_app
