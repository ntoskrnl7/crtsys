#pragma once

#include <cstdint>

#include <ntl/deps/zpp/serializer.h>

struct ntl_rpc_sample_request {
  std::uint32_t value = 0;
  std::uint32_t bias = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value, self.bias);
  }
};

struct ntl_rpc_sample_reply {
  std::uint32_t value = 0;
  std::uint32_t doubled = 0;
  std::uint32_t biased = 0;
  std::uint32_t server_irql = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value, self.doubled, self.biased, self.server_irql);
  }
};
