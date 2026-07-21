#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ntl/rpc/common>

enum class rpc_stream_action : std::uint32_t {
  echo = 1,
  burst = 2,
  complete = 3,
  fail = 4,
  delayed_echo = 5,
  publish_priorities = 6,
};

struct rpc_priority_event {
  std::uint32_t value = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value);
  }
};

struct rpc_stream_upload {
  rpc_stream_action action = rpc_stream_action::echo;
  std::uint64_t sequence = 0;
  std::uint32_t count = 1;
  std::uint32_t delay_ms = 0;
  std::string text;
  std::vector<std::uint32_t> values;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.action, self.sequence, self.count, self.delay_ms, self.text,
            self.values);
  }
};

struct rpc_stream_download {
  std::uint64_t sequence = 0;
  std::string text;
  std::vector<std::uint32_t> values;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.sequence, self.text, self.values);
  }
};

struct rpc_stream_stats {
  std::uint32_t uploads = 0;
  std::uint32_t queued = 0;
  std::uint32_t backpressured = 0;
  std::uint32_t cancellations = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.uploads, self.queued, self.backpressured, self.cancellations);
  }
};

namespace crtsys_rpc_streaming {

constexpr std::uint32_t contract_version = 1;

namespace capabilities {
constexpr std::uint64_t bidirectional = 1ull << 0;
constexpr std::uint64_t bounded_backpressure = 1ull << 1;
constexpr std::uint64_t reconnect_replay = 1ull << 2;
constexpr std::uint64_t cancellation = 1ull << 3;
constexpr std::uint64_t batching_and_priority = 1ull << 4;
constexpr std::uint64_t current = bidirectional | bounded_backpressure |
                                  reconnect_replay | cancellation |
                                  batching_and_priority;
} // namespace capabilities

constexpr auto records =
    ntl::rpc::stream<0xB40, rpc_stream_upload, rpc_stream_download, 64 * 1024,
                     64 * 1024, 256 * 1024>{};

constexpr auto query_stats = ntl::rpc::method<0xB41, rpc_stream_stats()>{};

constexpr auto background_events =
    ntl::rpc::notification<0xB42, rpc_priority_event>{}
        .with_priority<ntl::rpc::delivery_priority::background>()
        .with_batch_records<2>();

constexpr auto critical_events =
    ntl::rpc::notification<0xB43, rpc_priority_event>{}
        .with_priority<ntl::rpc::delivery_priority::critical>()
        .with_batch_records<2>();

} // namespace crtsys_rpc_streaming
