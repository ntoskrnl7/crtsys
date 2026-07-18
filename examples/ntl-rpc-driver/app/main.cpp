#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <vector>

#include <ntl/rpc/client>

#include "ntl_rpc_sample.hpp"

namespace {

std::uint32_t parse_value(int argc, wchar_t **argv, int index,
                          std::uint32_t fallback) {
  if (argc <= index) {
    return fallback;
  }

  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(argv[index], &end, 0);
  if (end == argv[index] || *end != L'\0') {
    std::fwprintf(stderr, L"invalid value: %ls\n", argv[index]);
    std::exit(2);
  }

  return static_cast<std::uint32_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  const std::uint32_t value = parse_value(argc, argv, 1, 21);
  const std::uint32_t bias = parse_value(argc, argv, 2, 7);

  try {
    ntl::rpc::client client(L"crtsys_ntl_rpc_sample");
    ntl::rpc::contract_requirements requirements;
    requirements.contract_version(
        crtsys_ntl_rpc_sample::rpc_contract_version)
        .capabilities(crtsys_ntl_rpc_sample::rpc_capabilities)
        .method(crtsys_ntl_rpc_sample::add_2_method)
        .method(crtsys_ntl_rpc_sample::describe_1_method)
        .method(crtsys_ntl_rpc_sample::series_1_method);
    (void)client.require_contract(requirements);

    const int sum = crtsys_ntl_rpc_sample::add(40, 2);
    if (sum != 42) {
      std::fwprintf(stderr, L"unexpected add result: %d\n", sum);
      return 1;
    }

    ntl_rpc_sample_request request{value, bias};
    const auto reply = crtsys_ntl_rpc_sample::describe(request);
    if (reply.value != value || reply.doubled != value * 2 ||
        reply.biased != value + bias || reply.server_irql != 0) {
      std::fwprintf(stderr,
                    L"unexpected describe reply: value=%u doubled=%u "
                    L"biased=%u server_irql=%u\n",
                    reply.value, reply.doubled, reply.biased,
                    reply.server_irql);
      return 1;
    }

    const auto values = client.invoke(crtsys_ntl_rpc_sample::series_1_method,
                                      std::uint32_t{4});
    const std::vector<std::uint32_t> expected{1, 2, 3, 4};
    if (values != expected) {
      std::fwprintf(stderr, L"unexpected series result\n");
      return 1;
    }

    std::wprintf(L"rpc ok: add=%d value=%u doubled=%u biased=%u "
                 L"server_irql=%u series=%zu\n",
                 sum, reply.value, reply.doubled, reply.biased,
                 reply.server_irql, values.size());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC failed: %s\n", error.what());
    return 1;
  }
}
