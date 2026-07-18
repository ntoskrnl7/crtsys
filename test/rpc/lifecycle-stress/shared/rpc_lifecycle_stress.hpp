#pragma once

#include <cstdint>
#include <vector>

#include <ntl/rpc/common>

struct rpc_lifecycle_stats {
  std::uint64_t completed_calls = 0;
  std::uint32_t active_calls = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.completed_calls, self.active_calls);
  }
};

namespace crtsys_rpc_lifecycle_stress {

constexpr std::uint32_t contract_version = 1;

namespace capabilities {
constexpr std::uint64_t handle_churn = 1ull << 0;
constexpr std::uint64_t in_flight_stop = 1ull << 1;
constexpr std::uint64_t current = handle_churn | in_flight_stop;
} // namespace capabilities

constexpr std::uint64_t ping_mask = 0x9E3779B97F4A7C15ull;

constexpr auto ping =
    ntl::rpc::method<0xB01, std::uint64_t(std::uint64_t)>{};
constexpr auto checksum =
    ntl::rpc::method<0xB02,
                     std::uint64_t(const std::vector<std::uint32_t> &)>{}
        .max_request_size<64 * 1024>()
        .max_decode_allocation<128 * 1024>();
constexpr auto slow_call =
    ntl::rpc::method<0xB03, std::uint32_t(std::uint32_t)>{};
constexpr auto query_stats =
    ntl::rpc::method<0xB04, rpc_lifecycle_stats()>{};

} // namespace crtsys_rpc_lifecycle_stress
