#include "managed_tcp_scenario.hpp"

#include <array>
#include <iostream>

using crtsys::wfp_kernel_browser_https::http1_evidence_complete;
using crtsys::wfp_kernel_browser_https::managed_tcp_acceptance_result;

int main() {
  managed_tcp_acceptance_result evidence{};
  evidence.http1_policy_pipeline = true;
  evidence.http1_pipelining = true;
  evidence.http1_compression = true;
  evidence.http1_grpc = true;
  evidence.http1_websocket = true;
  evidence.http1_ipv4_ipv6_wfp = true;
  evidence.origin_system_validation = true;
  evidence.origin_exact_pin = true;
  evidence.origin_mtls = true;
  evidence.capture_records = 11;
  if (!http1_evidence_complete(evidence))
    return 1;
  constexpr std::array fields{
      &managed_tcp_acceptance_result::http1_policy_pipeline,
      &managed_tcp_acceptance_result::http1_pipelining,
      &managed_tcp_acceptance_result::http1_compression,
      &managed_tcp_acceptance_result::http1_grpc,
      &managed_tcp_acceptance_result::http1_websocket,
      &managed_tcp_acceptance_result::http1_ipv4_ipv6_wfp,
      &managed_tcp_acceptance_result::origin_system_validation,
      &managed_tcp_acceptance_result::origin_exact_pin,
      &managed_tcp_acceptance_result::origin_mtls};
  for (const auto field : fields) {
    auto missing = evidence;
    missing.*field = false;
    if (http1_evidence_complete(missing))
      return 2;
  }
  evidence.capture_records = 10;
  if (http1_evidence_complete(evidence))
    return 3;
  std::cout << "Kernel browser HTTP/1 evidence contract PASS: "
               "policy=pass compression=pass grpc=pass websocket=pass "
               "ipv4_ipv6_wfp=pass pipelining=pass tls_origin=pass\n";
  return 0;
}
