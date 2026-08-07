#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <winioctl.h>

#include <cstdint>
#include <functional>
#include <string_view>

#include "acceptance_controller.hpp"
#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

class capture_log;

struct managed_tcp_acceptance_result {
  bool http1_policy_pipeline = false;
  bool http1_pipelining = false;
  bool http1_compression = false;
  bool http1_grpc = false;
  bool http1_websocket = false;
  bool http1_ipv4_ipv6_wfp = false;
  bool http2_policy_pipeline = false;
  bool http2_multiplexing = false;
  bool http2_flow_control = false;
  bool http2_goaway = false;
  bool http2_compression = false;
  bool http2_grpc = false;
  bool http2_websocket = false;
  bool http2_extended_connect = false;
  bool http2_unsupported_connect_fail_closed = false;
  bool http2_ipv4_ipv6_wfp = false;
  bool origin_system_validation = false;
  bool origin_exact_pin = false;
  bool origin_mtls = false;
  std::uint64_t capture_records = 0;
};

inline bool http1_evidence_complete(
    const managed_tcp_acceptance_result &value) noexcept {
  return value.http1_policy_pipeline && value.http1_pipelining &&
         value.http1_compression && value.http1_grpc &&
         value.http1_websocket &&
         value.http1_ipv4_ipv6_wfp && value.origin_system_validation &&
         value.origin_exact_pin && value.origin_mtls &&
         value.capture_records >= 11;
}

inline bool http2_evidence_complete(
    const managed_tcp_acceptance_result &value) noexcept {
  return value.http2_policy_pipeline && value.http2_multiplexing &&
         value.http2_flow_control && value.http2_goaway &&
         value.http2_compression && value.http2_grpc &&
         value.http2_websocket && value.http2_extended_connect &&
         value.http2_unsupported_connect_fail_closed &&
         value.http2_ipv4_ipv6_wfp &&
         value.origin_system_validation && value.origin_exact_pin &&
         value.origin_mtls && value.capture_records >= 25;
}

managed_tcp_acceptance_result run_managed_tcp_acceptance(
    acceptance_controller &controller,
    const wfp_kernel_browser_https_inspection::service_info &service,
    capture_log &logger, std::uint64_t capture_baseline,
    PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate);

struct managed_http3_fallback_acceptance_result {
  bool http2 = false;
  bool http1 = false;
  bool non_safe_rejected = false;
};

managed_http3_fallback_acceptance_result
run_managed_http3_fallback_acceptance(
    acceptance_controller &controller,
    PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate);

struct dynamic_sni_acceptance_result {
  bool second_name = false;
  bool replacement = false;
  bool observed_peer_leaf_change = false;
  bool active_session_lifetime = false;
  bool unknown_name_failed_closed = false;
};

dynamic_sni_acceptance_result run_dynamic_sni_acceptance(
    acceptance_controller &controller,
    const wfp_kernel_browser_https_inspection::service_info &service,
    std::string_view server_name, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate,
    const std::function<void()> &replace_identity);

} // namespace crtsys::wfp_kernel_browser_https
