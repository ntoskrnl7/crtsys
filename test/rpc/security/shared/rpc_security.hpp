#pragma once

#include <cstdint>

#include <ntl/rpc/common>

struct rpc_security_caller_info {
  std::uint64_t process_id = 0;
  std::uint32_t user_mode = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.process_id, self.user_mode);
  }
};

namespace crtsys_rpc_security {

constexpr auto caller_info =
    ntl::rpc::method<0xC80, rpc_security_caller_info()>{};
constexpr auto unrestricted_echo =
    ntl::rpc::method<0xC81, std::uint32_t(std::uint32_t)>{};
constexpr auto authenticated_user_only =
    ntl::rpc::method<0xC82, std::uint32_t(std::uint32_t)>{};
constexpr auto descriptor_allowed =
    ntl::rpc::method<0xC83, std::uint32_t(std::uint32_t)>{};
constexpr auto descriptor_denied =
    ntl::rpc::method<0xC84, std::uint32_t(std::uint32_t)>{};
constexpr auto denied_callback_count =
    ntl::rpc::method<0xC85, std::uint32_t()>{};

} // namespace crtsys_rpc_security
