#pragma once

#include <ntl/flt/communication_client>

#include <filesystem>
#include <string>

namespace crtsys_flt_runtime_test {

bool run_operation_status_runtime_tests(ntl::flt::communication_client &client,
                                        const std::filesystem::path &root,
                                        std::string &failure);

} // namespace crtsys_flt_runtime_test
