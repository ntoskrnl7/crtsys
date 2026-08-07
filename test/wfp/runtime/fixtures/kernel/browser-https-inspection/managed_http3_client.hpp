#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crtsys::wfp_kernel_browser_https {

struct response {
  unsigned status = 0;
  std::string content_encoding;
  std::vector<std::byte> body;
  bool dynamic_qpack_acknowledged = false;
};

response exchange_http3(int address_family, std::uint16_t port,
                        std::string_view path, bool block,
                        bool dynamic_qpack = true,
                        std::string_view authority = "localhost");

void require_http3_origin_handshake_direct(
    int address_family, std::uint16_t port,
    std::span<const std::byte> client_certificate_thumbprint);

response exchange_http3_grpc(
    int address_family, std::uint16_t port,
    std::string_view authority = "localhost");

struct http3_multiplex_result {
  bool one_connection = false;
  bool peer_settings = false;
  bool concurrent_streams = false;
  bool reverse_completion = false;
  bool stream_local_block = false;
  bool stream_local_reset = false;
  bool aggregate_quota = false;
  bool transformed = false;
  bool clean_drain = false;
  std::uint64_t request_streams = 0;
  std::string completion_order;
};

http3_multiplex_result exercise_http3_multiplex(
    int address_family, std::uint16_t port,
    std::string_view authority = "localhost");

struct webtransport_result {
  bool connected = false;
  bool settings = false;
  bool extended_connect = false;
  bool bidirectional = false;
  bool unidirectional = false;
  bool datagram = false;
  bool capsule = false;
  std::uint64_t capsules = 0;
  bool reliable_reset = false;
};

webtransport_result exercise_webtransport(
    int address_family, std::uint16_t port,
    std::string_view authority = "localhost");

struct http3_negative_acceptance_result {
  bool webtransport_policy_rejected = false;
  bool webtransport_client_inactive = false;
  bool websocket_extended_connect_rejected = false;
  bool unknown_extended_connect_rejected = false;
  bool uppercase_header_rejected = false;
  bool connection_header_rejected = false;
  bool bad_te_header_rejected = false;
};

http3_negative_acceptance_result
exercise_http3_negative_acceptance(int address_family, std::uint16_t port,
                                   std::string_view authority = "localhost");

} // namespace crtsys::wfp_kernel_browser_https
