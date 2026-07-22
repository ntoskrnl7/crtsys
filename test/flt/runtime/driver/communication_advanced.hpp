#pragma once

#include <ntl/flt/communication>

namespace ntl {
namespace flt {
class driver;
}
} // namespace ntl

namespace crtsys_flt_runtime_test {

void configure_advanced_communication_tests(
    ntl::flt::communication_server &server,
    const ntl::flt::communication_publisher &publisher);

ntl::status add_drop_policy_test_port(ntl::flt::driver &driver);
ntl::status add_byte_quota_test_port(ntl::flt::driver &driver);
ntl::status add_reject_test_port(ntl::flt::driver &driver);
ntl::status add_connection_limit_test_port(ntl::flt::driver &driver);
ntl::status add_session_limit_test_port(ntl::flt::driver &driver);

} // namespace crtsys_flt_runtime_test
