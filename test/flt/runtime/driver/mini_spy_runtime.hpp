#pragma once

#include "../shared/runtime_test.hpp"

#include <ntl/flt/communication>

#include <cstdint>

namespace crtsys_flt_runtime_test {

void initialize_mini_spy_runtime() noexcept;
void configure_mini_spy_runtime_messages(
    ntl::flt::communication_server &server);
void record_mini_spy_operation(mini_spy_operation operation,
                               mini_spy_phase phase, NTSTATUS status,
                               std::uint32_t information = 0) noexcept;
mini_spy_batch close_mini_spy_runtime() noexcept;

} // namespace crtsys_flt_runtime_test
