#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>

#include <ntl/rpc/client>

#include "ntl_rpc_sample.hpp"
#include "examples.hpp"

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
        .method(crtsys_ntl_rpc_sample::series_1_method)
        .method(crtsys_ntl_rpc_sample::delayed_add_3_method);
    (void)client.require_contract(requirements);

    crtsys_ntl_rpc_sample_app::run_synchronous_calls(client, value, bias);
    crtsys_ntl_rpc_sample_app::run_caller_security();
    crtsys_ntl_rpc_sample_app::run_asynchronous_call();
    crtsys_ntl_rpc_sample_app::run_cancellation();
    crtsys_ntl_rpc_sample_app::run_coroutine_call();
    crtsys_ntl_rpc_sample_app::run_stop_token_cancellation();
    crtsys_ntl_rpc_sample_app::run_reliable_notifications();
    std::wprintf(L"all RPC examples completed\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "RPC failed: %s\n", error.what());
    return 1;
  }
}
