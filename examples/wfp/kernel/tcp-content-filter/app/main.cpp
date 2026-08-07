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

#include "content_filter_control.hpp"
#include "tcp_content_filter_contract.hpp"

namespace {

namespace contract = wfp_kernel_tcp_content_filter;
namespace control = crtsys::examples::wfp::content_filter::control;

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
  HANDLE value_;
};

contract::filter_stats query_stats(HANDLE device) {
  contract::filter_stats stats{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::query_stats_ioctl, nullptr, 0,
                         &stats, sizeof(stats), &bytes, nullptr) ||
      bytes != sizeof(stats))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(query TCP filter stats)");
  return stats;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t destination_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key, L"crtsys kernel TCP content-filter provider",
         L"Kernel-owned bounded application-message policy"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key, L"crtsys kernel TCP content-filter sublayer",
         L"Fail-closed kernel stream inspection", 0x7600});
    const auto flow_v4 = transaction.add_callout<contract::flow_layer_v4>(
        provider, {contract::flow_callout_key_v4,
                   L"Attach IPv4 kernel TCP policy state", L""});
    const auto stream_v4 = transaction.add_callout<contract::stream_layer_v4>(
        provider, {contract::stream_callout_key_v4,
                   L"Inspect IPv4 messages in the kernel", L""});
    const auto flow_v6 = transaction.add_callout<contract::flow_layer_v6>(
        provider, {contract::flow_callout_key_v6,
                   L"Attach IPv6 kernel TCP policy state", L""});
    const auto stream_v6 = transaction.add_callout<contract::stream_layer_v6>(
        provider, {contract::stream_callout_key_v6,
                   L"Inspect IPv6 messages in the kernel", L""});

    ntl::wfp::arbitration_filter_builder<contract::flow_layer_v4> flow_filter_v4(
        contract::flow_filter_key_v4, L"Select inbound IPv4 TCP server flows",
        ntl::wfp::callout_unavailable::block);
    flow_filter_v4.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_arbitration_filter(sublayer, flow_v4, flow_filter_v4);
    ntl::wfp::stream_filter_builder<contract::stream_layer_v4>
        stream_filter_v4(contract::stream_filter_key_v4,
                         L"Apply kernel content policy to IPv4 TCP",
                         ntl::wfp::callout_unavailable::block);
    stream_filter_v4.local_port_equal(destination_port);
    transaction.add_stream_filter(sublayer, stream_v4,
                                          stream_filter_v4);

    ntl::wfp::arbitration_filter_builder<contract::flow_layer_v6> flow_filter_v6(
        contract::flow_filter_key_v6, L"Select inbound IPv6 TCP server flows",
        ntl::wfp::callout_unavailable::block);
    flow_filter_v6.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_arbitration_filter(sublayer, flow_v6, flow_filter_v6);
    ntl::wfp::stream_filter_builder<contract::stream_layer_v6>
        stream_filter_v6(contract::stream_filter_key_v6,
                         L"Apply kernel content policy to IPv6 TCP",
                         ntl::wfp::callout_unavailable::block);
    stream_filter_v6.local_port_equal(destination_port);
    transaction.add_stream_filter(sublayer, stream_v6,
                                          stream_filter_v6);
  });
}

std::string stats_text(const contract::filter_stats &before,
                       const contract::filter_stats &after) {
  std::ostringstream output;
  output << "before.inspected=" << before.inspected << '\n'
         << "before.permitted=" << before.permitted << '\n'
         << "before.blocked=" << before.blocked << '\n'
         << "before.malformed=" << before.malformed << '\n'
         << "before.failed=" << before.failed << '\n'
         << "after.inspected=" << after.inspected << '\n'
         << "after.permitted=" << after.permitted << '\n'
         << "after.blocked=" << after.blocked << '\n'
         << "after.malformed=" << after.malformed << '\n'
         << "after.failed=" << after.failed << '\n';
  return output.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto options = control::parse_options(argc, argv, false);
    handle_owner device(::CreateFileW(
        contract::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(::GetLastError(), std::system_category(),
                              "open kernel TCP content-filter device");
    const auto before = query_stats(device.get());
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys kernel TCP content-filter controller");
    install_policy(policy, options.port);
    control::signal_ready(options);
    control::wait_for_stop(options);
    const auto after = query_stats(device.get());
    control::write_file(options.stats_file, stats_text(before, after));
    std::wcout << L"Kernel TCP content-filter controller stopped: port="
               << options.port << L", policy removed on exit.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel TCP content-filter controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
