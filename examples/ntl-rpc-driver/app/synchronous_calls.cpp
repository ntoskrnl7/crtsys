#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

#include <ntl/rpc/client>

#include "ntl_rpc_sample.hpp"
#include "examples.hpp"

namespace crtsys_ntl_rpc_sample_app {

void run_synchronous_calls(ntl::rpc::client &client, std::uint32_t value,
                           std::uint32_t bias) {
  // Generated wrappers are the shortest form for occasional synchronous
  // calls. Each wrapper opens the endpoint and waits for its result.
  const int sum = crtsys_ntl_rpc_sample::add(40, 2);
  if (sum != 42) {
    throw std::runtime_error("unexpected add result");
  }

  const ntl_rpc_sample_request request{value, bias};
  const auto reply = crtsys_ntl_rpc_sample::describe(request);
  if (reply.value != value || reply.doubled != value * 2 ||
      reply.biased != value + bias || reply.server_irql != 0) {
    throw std::runtime_error("unexpected describe reply");
  }

  // A reusable client avoids reopening the device for repeated calls.
  const auto values = client.invoke(crtsys_ntl_rpc_sample::series_1_method,
                                    std::uint32_t{4});
  const std::vector<std::uint32_t> expected{1, 2, 3, 4};
  if (values != expected) {
    throw std::runtime_error("unexpected series result");
  }

  std::wprintf(L"sync: add=%d value=%u doubled=%u biased=%u "
               L"server_irql=%u series=%zu\n",
               sum, reply.value, reply.doubled, reply.biased,
               reply.server_irql, values.size());
}

} // namespace crtsys_ntl_rpc_sample_app
