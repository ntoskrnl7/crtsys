#include "managed_http3_evidence.hpp"

#include <array>
#include <iostream>

using crtsys::wfp_kernel_browser_https::http3_evidence_complete;
using crtsys::wfp_kernel_browser_https::managed_http3_acceptance_result;

int main() {
  managed_http3_acceptance_result evidence{};
  evidence.policy_pipeline = true;
  evidence.compression = true;
  evidence.grpc = true;
  evidence.dynamic_qpack = true;
  evidence.inbound_peer_settings = true;
  evidence.origin_h3_negotiated = true;
  evidence.origin_peer_settings = true;
  evidence.origin_qpack_acknowledgement = true;
  evidence.origin_single_qpack_encoder_stream = true;
  evidence.webtransport = true;
  evidence.capsule_stream = true;
  evidence.multiplex = true;
  evidence.reverse_completion = true;
  evidence.stream_local_block = true;
  evidence.stream_local_reset = true;
  evidence.aggregate_quota = true;
  evidence.multiplex_clean_drain = true;
  evidence.origin_system_validation = true;
  evidence.origin_exact_pin = true;
  evidence.origin_mtls = true;
  evidence.webtransport_policy_rejected = true;
  evidence.webtransport_rejection_no_session = true;
  evidence.unsupported_extended_connect_fail_closed = true;
  evidence.invalid_request_headers_fail_closed = true;
  evidence.negative_origin_isolation = true;
  evidence.capture_records = 13;
  evidence.peak_pending_requests = 4;
  evidence.peak_buffered_request_bytes = 2 * 1024 * 1024;
  evidence.buffer_quota_rejections = 1;
  evidence.canceled_streams = 2;
  if (!http3_evidence_complete(evidence))
    return 1;
  constexpr std::array fields{
      &managed_http3_acceptance_result::policy_pipeline,
      &managed_http3_acceptance_result::compression,
      &managed_http3_acceptance_result::grpc,
      &managed_http3_acceptance_result::dynamic_qpack,
      &managed_http3_acceptance_result::inbound_peer_settings,
      &managed_http3_acceptance_result::origin_h3_negotiated,
      &managed_http3_acceptance_result::origin_peer_settings,
      &managed_http3_acceptance_result::origin_qpack_acknowledgement,
      &managed_http3_acceptance_result::origin_single_qpack_encoder_stream,
      &managed_http3_acceptance_result::webtransport,
      &managed_http3_acceptance_result::capsule_stream,
      &managed_http3_acceptance_result::multiplex,
      &managed_http3_acceptance_result::reverse_completion,
      &managed_http3_acceptance_result::stream_local_block,
      &managed_http3_acceptance_result::stream_local_reset,
      &managed_http3_acceptance_result::aggregate_quota,
      &managed_http3_acceptance_result::multiplex_clean_drain,
      &managed_http3_acceptance_result::origin_system_validation,
      &managed_http3_acceptance_result::origin_exact_pin,
      &managed_http3_acceptance_result::origin_mtls,
      &managed_http3_acceptance_result::webtransport_policy_rejected,
      &managed_http3_acceptance_result::webtransport_rejection_no_session,
      &managed_http3_acceptance_result::unsupported_extended_connect_fail_closed,
      &managed_http3_acceptance_result::invalid_request_headers_fail_closed,
      &managed_http3_acceptance_result::negative_origin_isolation};
  for (const auto field : fields) {
    auto missing = evidence;
    missing.*field = false;
    if (http3_evidence_complete(missing))
      return 2;
  }
  evidence.capture_records = 12;
  if (http3_evidence_complete(evidence))
    return 3;
  std::cout << "Kernel browser HTTP/3 evidence contract PASS: "
               "policy=pass compression=pass grpc=pass qpack=pass settings=pass "
               "origin_h3=pass origin_settings=pass origin_qpack_ack=pass "
               "origin_single_qpack_encoder=pass "
               "webtransport=pass capsule=pass multiplex=pass "
               "webtransport_policy_rejection=pass "
               "unsupported_connect=pass invalid_headers=pass "
               "origin_isolation=pass cancel=pass quota=pass tls_origin=pass\n";
  return 0;
}
