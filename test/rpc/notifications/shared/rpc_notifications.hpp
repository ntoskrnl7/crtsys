#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ntl/rpc/common>

struct rpc_notification_message {
  std::uint64_t sequence = 0;
  std::string text;
  std::vector<std::uint32_t> values;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.sequence, self.text, self.values);
  }
};

struct rpc_notification_stats {
  std::uint32_t delivered = 0;
  std::uint32_t dropped = 0;
  std::uint32_t sessions_opened = 0;
  std::uint32_t sessions_resumed = 0;
  std::uint32_t sessions_disconnected = 0;
  std::uint32_t sessions_closed = 0;
  std::uint32_t persisted = 0;
  std::uint32_t acknowledged = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.delivered, self.dropped, self.sessions_opened,
            self.sessions_resumed, self.sessions_disconnected,
            self.sessions_closed, self.persisted, self.acknowledged);
  }
};

namespace crtsys_rpc_notifications {

constexpr std::uint32_t contract_version = 1;

namespace capabilities {
constexpr std::uint64_t typed_payload = 1ull << 0;
constexpr std::uint64_t cancellation = 1ull << 1;
constexpr std::uint64_t cross_bitness = 1ull << 2;
constexpr std::uint64_t lifecycle = 1ull << 3;
constexpr std::uint64_t current =
    typed_payload | cancellation | cross_bitness | lifecycle;
} // namespace capabilities

constexpr auto progress =
    ntl::rpc::notification<0x1001, rpc_notification_message>{}
        .max_response_size<64 * 1024>()
        .max_decode_allocation<128 * 1024>();

constexpr auto publish =
    ntl::rpc::method<0xB20, bool(std::uint64_t, std::uint32_t)>{};
constexpr auto publish_at_dispatch =
    ntl::rpc::method<0xB21, std::int32_t(std::uint64_t)>{};
constexpr auto query_stats =
    ntl::rpc::method<0xB22, rpc_notification_stats()>{};
constexpr auto publish_reliable =
    ntl::rpc::method<0xB23, std::int32_t(std::uint64_t, std::uint32_t)>{};
constexpr auto hold_session =
    ntl::rpc::method<0xB24, std::uint64_t(std::uint32_t)>{};

} // namespace crtsys_rpc_notifications
