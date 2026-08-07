#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"
#include "connect_redirect_policy.hpp"
#include "controller_lifecycle.hpp"

namespace {

namespace contract = wfp_kernel_connect_redirect;

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }
private:
  HANDLE value_;
};

std::uint16_t parse_port(const wchar_t *value) {
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (!value[0] || !end || *end || parsed == 0 || parsed > 65535)
    throw std::invalid_argument("port must be in 1..65535");
  return static_cast<std::uint16_t>(parsed);
}

contract::proxy_info query_proxy(HANDLE device) {
  contract::proxy_info info{};
  DWORD bytes = 0;
  if (!DeviceIoControl(device, contract::query_proxy_ioctl, nullptr, 0,
                       &info, sizeof(info), &bytes, nullptr) ||
      bytes != sizeof(info))
    throw std::system_error(GetLastError(), std::system_category(),
                            "DeviceIoControl(query proxy)");
  return info;
}

void install_policy(ntl::wfp::policy_session &session,
                    const contract::proxy_info &proxy,
                    std::uint16_t original_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key, L"crtsys kernel connect-redirect provider",
         L"Redirects selected TCP connects to WSK kernel listeners"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key, L"crtsys kernel connect-redirect sublayer",
         L"Kernel-local dual-stack proxy policy", 0x7620});
    const auto callout_v4 = transaction.add_callout<contract::layer_v4>(
        provider, {contract::callout_key_v4,
                   L"Redirect IPv4 connect to kernel WSK", L""});
    const auto callout_v6 = transaction.add_callout<contract::layer_v6>(
        provider, {contract::callout_key_v6,
                   L"Redirect IPv6 connect to kernel WSK", L""});
    auto filter_v4 =
        crtsys::examples::wfp::connect_redirect::make_filter<
            contract::layer_v4>(
            contract::filter_key_v4, L"Redirect selected IPv4 connect",
            proxy.process_id, proxy.port_v4, original_port);
    transaction.add_connect_redirect_filter(sublayer, callout_v4, filter_v4);
    auto filter_v6 =
        crtsys::examples::wfp::connect_redirect::make_filter<
            contract::layer_v6>(
            contract::filter_key_v6, L"Redirect selected IPv6 connect",
            proxy.process_id, proxy.port_v6, original_port);
    transaction.add_connect_redirect_filter(sublayer, callout_v6, filter_v6);
  });
}

std::string format_stats(
    std::string_view state,
    const contract::proxy_info &info) {
  std::ostringstream value;
  value << "state=" << state << "\n"
        << "proxy_pid=" << info.process_id << "\n"
        << "proxy_port_v4=" << info.port_v4 << "\n"
        << "proxy_port_v6=" << info.port_v6 << "\n"
        << "accepted=" << info.accepted << "\n"
        << "redirect_records=" << info.redirect_records << "\n"
        << "completed=" << info.completed << "\n"
        << "failed=" << info.failed << "\n"
        << "bytes_to_origin=" << info.bytes_to_origin << "\n"
        << "bytes_to_client=" << info.bytes_to_client << "\n";
  return value.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: crtsys_wfp_kernel_connect_redirect_controller.exe "
          "<origin-port> <ipc-directory>");
    const auto original_port = parse_port(argv[1]);
    crtsys::wfp_sample::controller_lifecycle lifecycle(argv[2]);
    handle_owner device(CreateFileW(
        contract::user_device_path, GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(GetLastError(), std::system_category(),
                              "CreateFileW(kernel proxy)");
    const auto before = query_proxy(device.get());
    if (!before.process_id || !before.port_v4 || !before.port_v6)
      throw std::runtime_error("kernel proxy listener is not ready");
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys kernel connect-redirect controller");
      install_policy(policy, before, original_port);
      lifecycle.publish_ready(format_stats("ready", before));
      lifecycle.wait_for_stop();
    }
    const auto after = query_proxy(device.get());
    lifecycle.publish_stats(format_stats("stopped", after));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "kernel connect-redirect controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
