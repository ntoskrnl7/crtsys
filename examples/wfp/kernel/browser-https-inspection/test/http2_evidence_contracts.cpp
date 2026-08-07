#include "managed_tcp_scenario.hpp"

#include <array>
#include <iostream>

using crtsys::wfp_kernel_browser_https::http2_evidence_complete;
using crtsys::wfp_kernel_browser_https::managed_tcp_acceptance_result;

int main() {
  managed_tcp_acceptance_result evidence{};
  evidence.http2_policy_pipeline = true;
  evidence.http2_multiplexing = true;
  evidence.http2_flow_control = true;
  evidence.http2_goaway = true;
  evidence.http2_compression = true;
  evidence.http2_grpc = true;
  evidence.http2_websocket = true;
  evidence.http2_extended_connect = true;
  evidence.http2_unsupported_connect_fail_closed = true;
  evidence.http2_ipv4_ipv6_wfp = true;
  evidence.origin_system_validation = true;
  evidence.origin_exact_pin = true;
  evidence.origin_mtls = true;
  evidence.capture_records = 25;
  if (!http2_evidence_complete(evidence))
    return 1;
  constexpr std::array fields{
      &managed_tcp_acceptance_result::http2_policy_pipeline,
      &managed_tcp_acceptance_result::http2_multiplexing,
      &managed_tcp_acceptance_result::http2_flow_control,
      &managed_tcp_acceptance_result::http2_goaway,
      &managed_tcp_acceptance_result::http2_compression,
      &managed_tcp_acceptance_result::http2_grpc,
      &managed_tcp_acceptance_result::http2_websocket,
      &managed_tcp_acceptance_result::http2_extended_connect,
      &managed_tcp_acceptance_result::http2_unsupported_connect_fail_closed,
      &managed_tcp_acceptance_result::http2_ipv4_ipv6_wfp,
      &managed_tcp_acceptance_result::origin_system_validation,
      &managed_tcp_acceptance_result::origin_exact_pin,
      &managed_tcp_acceptance_result::origin_mtls};
  for (const auto field : fields) {
    auto missing = evidence;
    missing.*field = false;
    if (http2_evidence_complete(missing))
      return 2;
  }
  evidence.capture_records = 24;
  if (http2_evidence_complete(evidence))
    return 3;
  std::cout << "Kernel browser HTTP/2 evidence contract PASS: "
               "policy=pass compression=pass grpc=pass websocket=pass "
               "unsupported_connect=blocked ipv4_ipv6_wfp=pass "
               "multiplex=pass flow_control=pass goaway=pass "
               "tls_origin=pass\n";
  return 0;
}
