#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <ntl/wfp/all>

#include "flow_monitor_contract.hpp"
#include "runtime_controller.hpp"

namespace {

namespace runtime = crtsys::examples::wfp::runtime;

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      ::CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

wfp_flow_monitor::monitor_stats query_stats(HANDLE device) {
  wfp_flow_monitor::monitor_stats stats{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, wfp_flow_monitor::query_stats_ioctl, nullptr,
                         0, &stats, sizeof(stats), &bytes, nullptr) ||
      bytes != sizeof(stats))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(query flow-monitor stats)");
  return stats;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t port_v4, std::uint16_t port_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_flow_monitor::provider_key,
         L"crtsys NTL WFP flow-monitor provider",
         L"Dynamic provider for selected outbound TCP flow telemetry"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_flow_monitor::sublayer_key,
                   L"crtsys NTL WFP flow-monitor sublayer",
                   L"Observation-only flow and stream filters", 0x7400});
    const auto flow_callout_v4 =
        transaction.add_callout<wfp_flow_monitor::flow_layer_v4>(
            provider, {wfp_flow_monitor::flow_callout_key_v4,
                       L"Attach IPv4 TCP monitor flow state", L""});
    const auto stream_callout_v4 =
        transaction.add_callout<wfp_flow_monitor::stream_layer_v4>(
            provider, {wfp_flow_monitor::stream_callout_key_v4,
                       L"Observe IPv4 TCP stream bytes", L""});
    const auto flow_callout_v6 =
        transaction.add_callout<wfp_flow_monitor::flow_layer_v6>(
            provider, {wfp_flow_monitor::flow_callout_key_v6,
                       L"Attach IPv6 TCP monitor flow state", L""});
    const auto stream_callout_v6 =
        transaction.add_callout<wfp_flow_monitor::stream_layer_v6>(
            provider, {wfp_flow_monitor::stream_callout_key_v6,
                       L"Observe IPv6 TCP stream bytes", L""});

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::flow_layer_v4>
        flow_filter_v4(wfp_flow_monitor::flow_filter_key_v4,
                       L"Observe the selected outbound IPv4 TCP flow");
    flow_filter_v4.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(port_v4);
    transaction.add_inspection_filter(sublayer, flow_callout_v4,
                                      flow_filter_v4);
    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::stream_layer_v4>
        stream_filter_v4(wfp_flow_monitor::stream_filter_key_v4,
                         L"Count bytes on the selected IPv4 TCP stream");
    stream_filter_v4.remote_port_equal(port_v4);
    transaction.add_inspection_filter(sublayer, stream_callout_v4,
                                      stream_filter_v4);

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::flow_layer_v6>
        flow_filter_v6(wfp_flow_monitor::flow_filter_key_v6,
                       L"Observe the selected outbound IPv6 TCP flow");
    flow_filter_v6.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(port_v6);
    transaction.add_inspection_filter(sublayer, flow_callout_v6,
                                      flow_filter_v6);
    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::stream_layer_v6>
        stream_filter_v6(wfp_flow_monitor::stream_filter_key_v6,
                         L"Count bytes on the selected IPv6 TCP stream");
    stream_filter_v6.remote_port_equal(port_v6);
    transaction.add_inspection_filter(sublayer, stream_callout_v6,
                                      stream_filter_v6);
  });
}

std::string stats_text(const wfp_flow_monitor::monitor_stats &before,
                       const wfp_flow_monitor::monitor_stats &after,
                       std::uint16_t port_v4, std::uint16_t port_v6) {
  std::ostringstream output;
  output << "policy.ipv4_port=" << port_v4 << '\n'
         << "policy.ipv6_port=" << port_v6 << '\n'
         << "before.flows_started=" << before.flows_started << '\n'
         << "before.flows_closed=" << before.flows_closed << '\n'
         << "before.stream_indications=" << before.stream_indications << '\n'
         << "before.stream_bytes=" << before.stream_bytes << '\n'
         << "before.missed_bytes=" << before.missed_bytes << '\n'
         << "after.flows_started=" << after.flows_started << '\n'
         << "after.flows_closed=" << after.flows_closed << '\n'
         << "after.stream_indications=" << after.stream_indications << '\n'
         << "after.stream_bytes=" << after.stream_bytes << '\n'
         << "after.missed_bytes=" << after.missed_bytes << '\n';
  return output.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    runtime::arguments arguments(argc, argv);
    const auto port_v4 = arguments.required_port(L"--ipv4-port");
    const auto port_v6 = arguments.required_port(L"--ipv6-port");
    const auto lifecycle = runtime::parse_lifecycle(arguments);
    arguments.finish();

    handle_owner device(::CreateFileW(
        wfp_flow_monitor::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(::GetLastError(), std::system_category(),
                              "CreateFile(flow-monitor)");
    const auto before = query_stats(device.get());
    auto current = before;
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp flow-monitor controller");
    install_policy(policy, port_v4, port_v6);
    runtime::signal_ready(lifecycle);
    runtime::wait_for_stop(lifecycle, [&] { current = query_stats(device.get()); });
    current = query_stats(device.get());
    runtime::write_file(lifecycle.stats_file,
                        stats_text(before, current, port_v4, port_v6));
    std::wcout << L"Flow-monitor controller stopped; telemetry captured and "
                  L"ephemeral policy removed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Flow-monitor controller failed: " << error.what() << '\n';
    return 1;
  }
}
