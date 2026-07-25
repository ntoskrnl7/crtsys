#pragma once

#include <ntl/flt/rpc>

#include <array>
#include <cstddef>
#include <cstdint>

namespace crtsys_minifilter_operation_log_sample {

enum class operation : std::uint32_t {
  create = 1,
  read = 2,
  write = 3,
  cleanup = 4,
  close = 5,
};

enum class phase : std::uint32_t {
  pre = 1,
  post = 2,
};

struct record {
  std::uint64_t sequence = 0;
  operation operation_id = operation::create;
  phase phase_id = phase::pre;
  std::int32_t status = 0;
  std::uint32_t information = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.sequence, self.operation_id, self.phase_id, self.status,
            self.information);
  }
};

inline constexpr std::size_t batch_capacity = 16;
inline constexpr std::size_t queue_capacity = 64;

struct record_batch {
  std::array<record, batch_capacity> records{};
  std::uint32_t count = 0;
  std::uint32_t remaining = 0;
  std::uint64_t dropped = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.records, self.count, self.remaining, self.dropped);
  }
};

inline constexpr ntl::rpc::method<0xA70, std::uint32_t()> reset_method{};
inline constexpr ntl::rpc::method<0xA71, record_batch()> read_method{};

} // namespace crtsys_minifilter_operation_log_sample

NTL_FLT_RPC_BEGIN_CONTRACT(crtsys_minifilter_operation_log_sample,
                           L"\\CrtSysMinifilterOperationLogSamplePort", 1, 0)
NTL_FLT_RPC_END(crtsys_minifilter_operation_log_sample)
