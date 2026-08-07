#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https::controller_protocol {

namespace contract = wfp_kernel_browser_https_inspection;

inline constexpr std::uint32_t version = 2;
inline constexpr std::size_t maximum_error_size = 512;

enum class operation : std::uint32_t {
  hello = 1,
  query_service = 2,
  read_inspection = 3,
  read_identity_request = 4,
  query_quic_telemetry = 5,
  configure_identity = 6,
  configure_origin_security = 7,
  arm_origin_security_rollback = 8,
  install_tcp_policy = 9,
  install_http3_policy = 10,
  clear_policy = 11,
  stop = 12,
};

struct tcp_policy_evidence {
  std::uint64_t application_id_hash = 0;
  std::uint64_t filter_id_v4 = 0;
  std::uint64_t filter_id_v6 = 0;
};

struct http3_policy_evidence {
  std::uint64_t application_id_hash = 0;
  std::uint64_t http3_filter_id_v4 = 0;
  std::uint64_t http3_filter_id_v6 = 0;
  std::uint64_t datagram_filter_id_v4 = 0;
  std::uint64_t datagram_filter_id_v6 = 0;
  std::uint64_t reverse_filter_id_v4 = 0;
  std::uint64_t reverse_filter_id_v6 = 0;
};

struct request {
  std::uint32_t protocol_version = version;
  std::uint32_t size = 0;
  operation code = operation::hello;
  std::uint32_t reserved = 0;
  std::uint64_t cursor = 0;
  std::uint16_t port_v4 = 0;
  std::uint16_t port_v6 = 0;
  std::uint32_t reserved2 = 0;
  contract::certificate_config identity{};
  contract::origin_security_config origin_security{};
};

struct response {
  std::uint32_t protocol_version = version;
  std::uint32_t size = 0;
  operation code = operation::hello;
  std::uint32_t failed = 0;
  std::array<char, maximum_error_size> error{};
  contract::service_info service{};
  contract::inspection_read_result inspection{};
  contract::identity_request_read_result identity_request{};
  contract::quic_telemetry quic{};
  tcp_policy_evidence tcp_policy{};
  http3_policy_evidence http3_policy{};
};

static_assert(std::is_trivially_copyable_v<request>);
static_assert(std::is_trivially_copyable_v<response>);

} // namespace crtsys::wfp_kernel_browser_https::controller_protocol
