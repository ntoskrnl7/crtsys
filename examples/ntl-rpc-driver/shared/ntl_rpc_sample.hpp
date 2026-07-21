#pragma once

#include <vector>

#include <ntl/rpc/common>

#include "ntl_rpc_sample_types.hpp"

// The same declaration expands to kernel callback registration when this file
// follows <ntl/rpc/server>, and to user-mode wrapper functions when it follows
// <ntl/rpc/client>.
namespace crtsys_ntl_rpc_sample {
constexpr auto progress =
    ntl::rpc::notification<0x1001, ntl_rpc_sample_progress>{};
constexpr auto publish_progress =
    ntl::rpc::method<0x904, bool(std::uint64_t)>{};
} // namespace crtsys_ntl_rpc_sample

NTL_RPC_BEGIN_CONTRACT(crtsys_ntl_rpc_sample, 3, 0x7ull)

NTL_ADD_CALLBACK_ID_2(crtsys_ntl_rpc_sample, 0x900, int, add, int, left, int,
                      right, {
                        return crtsys_ntl_rpc_sample_server::add(left, right);
                      })

NTL_ADD_CALLBACK_ID_1(crtsys_ntl_rpc_sample, 0x901, ntl_rpc_sample_reply,
                      describe, const ntl_rpc_sample_request &, request, {
                        return crtsys_ntl_rpc_sample_server::describe(request);
                      })

NTL_ADD_CALLBACK_ID_1(
    crtsys_ntl_rpc_sample, 0x902,
    NTL_RPC_BOUNDED_RESPONSE(64 * 1024, std::vector<std::uint32_t>), series,
    std::uint32_t, count, {
      return crtsys_ntl_rpc_sample_server::series(count);
    })

NTL_ADD_AUTHORIZED_CALLBACK_CONTEXT_ID_3(
    crtsys_ntl_rpc_sample, 0x903, int, delayed_add,
    crtsys_ntl_rpc_sample_server::authorize_user_mode, call, std::uint32_t,
    milliseconds, int, left, int, right, {
      return crtsys_ntl_rpc_sample_server::delayed_add(
          call, milliseconds, left, right);
    })

NTL_ADD_STREAM_ID(
    crtsys_ntl_rpc_sample, 0x905, messages,
    ntl_rpc_sample_stream_upload, upload,
    ntl_rpc_sample_stream_download, stream, {
      ntl_rpc_sample_stream_download reply;
      reply.sequence = upload.sequence;
      reply.text = "driver received: " + upload.text;
      stream.write(reply);
      if (upload.finish)
        stream.complete();
    })

NTL_RPC_END(crtsys_ntl_rpc_sample)
