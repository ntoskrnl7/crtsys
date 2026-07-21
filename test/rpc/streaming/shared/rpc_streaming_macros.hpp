#pragma once

#include <cstdint>

#include <ntl/rpc/common>

struct rpc_macro_upload {
  std::uint32_t value = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value);
  }
};

struct rpc_macro_download {
  std::uint32_t value = 0;

  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.value);
  }
};

NTL_RPC_BEGIN_CONTRACT(crtsys_rpc_streaming_macros, 1, 0)

NTL_ADD_STREAM_ID(crtsys_rpc_streaming_macros, 0xB50, echo_stream,
                  rpc_macro_upload, upload, rpc_macro_download, stream, {
                    rpc_macro_download reply;
                    reply.value = upload.value;
                    stream.write(reply);
                    stream.complete();
                  })

NTL_ADD_AUTHORIZED_STREAM_ID(
    crtsys_rpc_streaming_macros, 0xB51, authorized_stream,
    [](const ntl::rpc::call_context &) noexcept { return STATUS_SUCCESS; },
    rpc_macro_upload, upload, rpc_macro_download, stream, {
      rpc_macro_download reply;
      reply.value = upload.value;
      stream.write(reply);
      stream.complete();
    })

NTL_RPC_END(crtsys_rpc_streaming_macros)
