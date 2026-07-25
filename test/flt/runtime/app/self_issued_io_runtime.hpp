#pragma once

#include <filesystem>
#include <string>

namespace ntl::flt {
class communication_client;
}

namespace crtsys_flt_runtime_test {

bool run_self_issued_io_runtime_tests(ntl::flt::communication_client &client,
                                      const std::filesystem::path &root,
                                      std::string &failure);

} // namespace crtsys_flt_runtime_test
