#pragma once

// Controlled traffic belongs to the runtime acceptance fixture, not the
// production controller.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crtsys::wfp_kernel_http3 {

struct response {
  unsigned status = 0;
  std::string content_encoding;
  std::vector<std::byte> body;
  bool dynamic_qpack_acknowledged = false;
};

response exchange_http3(int address_family, std::uint16_t port,
                        std::string_view path, bool block,
                        bool dynamic_qpack = true);

struct webtransport_result {
  bool connected = false;
  bool settings = false;
  bool extended_connect = false;
  bool bidirectional = false;
  bool unidirectional = false;
  bool datagram = false;
  bool capsule = false;
  bool reliable_reset = false;
  bool blocked = false;
};

webtransport_result exercise_webtransport(int address_family,
                                          std::uint16_t port);
bool exercise_blocked_webtransport(int address_family,
                                   std::uint16_t port);

bool http3_connect_is_blocked(int address_family, std::uint16_t port,
                              std::uint32_t timeout_milliseconds = 1500);

} // namespace crtsys::wfp_kernel_http3
