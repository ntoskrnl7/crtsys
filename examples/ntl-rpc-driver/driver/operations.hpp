#pragma once

#include <cstdint>
#include <vector>

#include <ntl/rpc/server>

#include "ntl_rpc_sample_types.hpp"

namespace crtsys_ntl_rpc_sample_server {

NTSTATUS authorize_user_mode(
    const ntl::rpc::call_context &caller) noexcept;

int add(int left, int right) noexcept;

ntl_rpc_sample_reply describe(const ntl_rpc_sample_request &request) noexcept;

std::vector<std::uint32_t> series(std::uint32_t count);

int delayed_add(ntl::rpc::call_context call, std::uint32_t milliseconds,
                int left, int right);

} // namespace crtsys_ntl_rpc_sample_server
