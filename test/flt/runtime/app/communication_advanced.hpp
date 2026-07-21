#pragma once

namespace ntl {
namespace flt {
class communication_client;
}
} // namespace ntl

namespace crtsys_flt_runtime_test {

bool run_advanced_communication_tests(ntl::flt::communication_client &client);

} // namespace crtsys_flt_runtime_test
