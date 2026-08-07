#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <ntl/wfp/all>

#include "datagram_proxy_contract.hpp"
#include "runtime_controller.hpp"

namespace {

namespace runtime = crtsys::examples::wfp::runtime;

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    runtime::arguments arguments(argc, argv);
    const auto original_port = arguments.required_port(L"--original-port");
    const auto proxy_port = arguments.required_port(L"--proxy-port");
    const auto application_path = arguments.required(L"--application");
    const auto lifecycle = runtime::parse_lifecycle(arguments);
    arguments.finish();

    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp datagram-proxy controller");
    ntl::wfp::transparent_udp_proxy_policy::install(
        policy, wfp_datagram_proxy::proxy_keys,
        {.application = ntl::wfp::application_id::from_path(application_path),
         .original_port = original_port,
         .local_proxy_port = proxy_port});
    runtime::signal_ready(lifecycle);
    runtime::wait_for_stop(lifecycle);

    std::ostringstream stats;
    stats << "policy.original_port=" << original_port << '\n'
          << "policy.proxy_port=" << proxy_port << '\n'
          << "policy.ipv4=" << 1 << '\n'
          << "policy.ipv6=" << 1 << '\n';
    runtime::write_file(lifecycle.stats_file, stats.str());
    std::wcout << L"Datagram-proxy controller stopped; ephemeral policy "
                  L"removed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Datagram-proxy controller failed: " << error.what()
              << '\n';
    return 1;
  }
}
