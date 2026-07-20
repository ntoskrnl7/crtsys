#pragma once

#include <cstdint>

#include <ntl/deps/zpp/serializer.h>
#include <ntl/rpc/common>

struct ntl_rpc_caller_info {
  std::uint64_t process_id = 0;
  std::uint32_t user_mode = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.process_id, self.user_mode);
  }
};

namespace crtsys_ntl_rpc_security {

constexpr auto caller_info = ntl::rpc::method<0x910, ntl_rpc_caller_info()>{};
constexpr auto user_mode_echo =
    ntl::rpc::method<0x911, std::uint32_t(std::uint32_t)>{};

} // namespace crtsys_ntl_rpc_security
