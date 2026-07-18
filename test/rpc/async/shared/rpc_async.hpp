#pragma once

#include <cstdint>
#include <vector>

#include <ntl/rpc/common>

struct rpc_async_stats {
  std::uint32_t started = 0;
  std::uint32_t completed = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.started, self.completed);
  }
};

namespace crtsys_rpc_async {

constexpr std::uint32_t contract_version = 1;

namespace capabilities {
constexpr std::uint64_t timeout = 1ull << 0;
constexpr std::uint64_t cancellation = 1ull << 1;
constexpr std::uint64_t concurrent_calls = 1ull << 2;
constexpr std::uint64_t pending_limit = 1ull << 3;
constexpr std::uint64_t current =
    timeout | cancellation | concurrent_calls | pending_limit;
} // namespace capabilities

constexpr auto delayed_value =
    ntl::rpc::method<0xB10,
                     std::uint32_t(std::uint32_t, std::uint32_t)>{};
constexpr auto delayed_void =
    ntl::rpc::method<0xB11, void(std::uint32_t)>{};
constexpr auto checksum =
    ntl::rpc::method<0xB12,
                     std::uint64_t(const std::vector<std::uint32_t> &)>{}
        .max_request_size<64 * 1024>()
        .max_decode_allocation<128 * 1024>();
constexpr auto query_stats =
    ntl::rpc::method<0xB13, rpc_async_stats()>{};

} // namespace crtsys_rpc_async
