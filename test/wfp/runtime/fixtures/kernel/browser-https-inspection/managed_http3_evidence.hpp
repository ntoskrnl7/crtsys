#pragma once

#include <cstdint>

namespace crtsys::wfp_kernel_browser_https {

struct managed_http3_acceptance_result {
  bool policy_pipeline = false;
  bool compression = false;
  bool grpc = false;
  bool dynamic_qpack = false;
  bool inbound_peer_settings = false;
  bool origin_h3_negotiated = false;
  bool origin_peer_settings = false;
  bool origin_qpack_acknowledgement = false;
  bool origin_single_qpack_encoder_stream = false;
  bool webtransport = false;
  bool capsule_stream = false;
  bool multiplex = false;
  bool reverse_completion = false;
  bool stream_local_block = false;
  bool stream_local_reset = false;
  bool aggregate_quota = false;
  bool multiplex_clean_drain = false;
  bool origin_system_validation = false;
  bool origin_exact_pin = false;
  bool origin_mtls = false;
  bool webtransport_policy_rejected = false;
  bool webtransport_rejection_no_session = false;
  bool unsupported_extended_connect_fail_closed = false;
  bool invalid_request_headers_fail_closed = false;
  bool negative_origin_isolation = false;
  std::uint64_t capture_records = 0;
  std::uint64_t peak_pending_requests = 0;
  std::uint64_t peak_buffered_request_bytes = 0;
  std::uint64_t buffer_quota_rejections = 0;
  std::uint64_t canceled_streams = 0;
};

inline bool http3_evidence_complete(
    const managed_http3_acceptance_result &value) noexcept {
  return value.policy_pipeline && value.compression && value.grpc &&
         value.dynamic_qpack && value.inbound_peer_settings &&
         value.origin_h3_negotiated && value.origin_peer_settings &&
         value.origin_qpack_acknowledgement &&
         value.origin_single_qpack_encoder_stream && value.webtransport &&
         value.capsule_stream &&
         value.multiplex && value.reverse_completion &&
         value.stream_local_block && value.stream_local_reset &&
         value.aggregate_quota && value.multiplex_clean_drain &&
         value.origin_system_validation && value.origin_exact_pin &&
         value.origin_mtls && value.webtransport_policy_rejected &&
         value.webtransport_rejection_no_session &&
         value.unsupported_extended_connect_fail_closed &&
         value.invalid_request_headers_fail_closed &&
         value.negative_origin_isolation && value.capture_records >= 13 &&
         value.peak_pending_requests >= 4 &&
         value.peak_buffered_request_bytes != 0 &&
         value.buffer_quota_rejections >= 1 && value.canceled_streams >= 2;
}

} // namespace crtsys::wfp_kernel_browser_https
