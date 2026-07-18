#pragma once

#include <cstdint>
#include <vector>

#include <ntl/rpc/common>

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

// The same declaration expands to kernel callback registration when this file
// follows <ntl/rpc/server>, and to user-mode wrapper functions when it follows
// <ntl/rpc/client>.
NTL_RPC_BEGIN_CONTRACT(crtsys_ntl_rpc_sample, 1, 0x1ull)

constexpr std::uint32_t series_max_count = 4096;

NTL_ADD_CALLBACK_2(crtsys_ntl_rpc_sample, int, add, int, left, int, right, {
                        DbgPrint("crtsys_ntl_rpc_sample::add kernel callback: "
                                 "left=%d right=%d\n",
                                 left, right);
                        return left + right;
                      })

NTL_ADD_CALLBACK_1(crtsys_ntl_rpc_sample, ntl_rpc_sample_reply, describe,
                   const ntl_rpc_sample_request &, request, {
                        ntl_rpc_sample_reply reply{};
                        reply.value = request.value;
                        reply.doubled = request.value * 2;
                        reply.biased = request.value + request.bias;
                        const auto server_irql = KeGetCurrentIrql();
                        reply.server_irql =
                            static_cast<std::uint32_t>(server_irql);
                        DbgPrint("crtsys_ntl_rpc_sample::describe kernel "
                                 "callback: value=%lu bias=%lu irql=%lu\n",
                                 static_cast<unsigned long>(request.value),
                                 static_cast<unsigned long>(request.bias),
                                 static_cast<unsigned long>(server_irql));
                        return reply;
                      })

NTL_ADD_CALLBACK_1(
    crtsys_ntl_rpc_sample,
    NTL_RPC_BOUNDED_RESPONSE(64 * 1024, std::vector<std::uint32_t>), series,
    std::uint32_t, count, {
      if (count > series_max_count)
        throw ntl::exception(STATUS_INVALID_PARAMETER, "series is too large");

      DbgPrint("crtsys_ntl_rpc_sample::series kernel callback: count=%lu\n",
               static_cast<unsigned long>(count));
      std::vector<std::uint32_t> values;
      values.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index)
        values.push_back(index + 1);
      return values;
    })

NTL_RPC_END(crtsys_ntl_rpc_sample)
