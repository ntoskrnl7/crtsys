#pragma once

#include <cstdint>
#include <vector>

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

// This header is shared by the driver and the user-mode app. When included
// after <ntl/rpc/server>, the callback bodies below are compiled as the
// kernel-mode server implementation and run in the driver's IOCTL dispatch
// path. When included after <ntl/rpc/client>, the same macro calls declare
// user-mode wrappers that send requests to the driver.
NTL_RPC_BEGIN(crtsys_ntl_rpc_sample)

NTL_ADD_CALLBACK_ID_2(crtsys_ntl_rpc_sample, 0x801, int, add, int, a, int, b, {
  DbgPrint("crtsys_ntl_rpc_sample::add kernel callback: a=%d b=%d\n", a, b);
  return a + b;
})

NTL_ADD_CALLBACK_ID_1(crtsys_ntl_rpc_sample, 0x802, ntl_rpc_sample_reply,
                      describe, const ntl_rpc_sample_request &, request, {
                        ntl_rpc_sample_reply reply{};
                        reply.value = request.value;
                        reply.doubled = request.value * 2;
                        reply.biased = request.value + request.bias;
                        // Kernel-only WDK API: this body runs in the driver,
                        // not in the user-mode wrapper generated from the same
                        // shared schema header.
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

NTL_ADD_CALLBACK_ID_1(crtsys_ntl_rpc_sample, 0x803,
                      NTL_RPC_ARG_PACK(std::vector<std::uint32_t>), series,
                      std::uint32_t, count, {
                        DbgPrint("crtsys_ntl_rpc_sample::series kernel "
                                 "callback: count=%lu\n",
                                 static_cast<unsigned long>(count));
                        std::vector<std::uint32_t> values;
                        values.reserve(count);
                        for (std::uint32_t i = 0; i < count; ++i) {
                          values.push_back(i + 1);
                        }
                        return values;
                      })

NTL_RPC_END(crtsys_ntl_rpc_sample)
