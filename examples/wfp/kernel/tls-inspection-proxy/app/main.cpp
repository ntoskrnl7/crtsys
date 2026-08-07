#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <ntl/net/tls/certificate>
#include <ntl/wfp/all>

#include "machine_certificate.hpp"
#include "runtime_controller.hpp"
#include "test_certificate.hpp"
#include "tls_inspection_proxy_contract.hpp"
#include "tls_runtime_control.hpp"
#include "windows_support.hpp"

namespace {

namespace contract = wfp_kernel_tls_inspection_proxy;
namespace runtime = crtsys::examples::wfp::runtime;
namespace tls_runtime = crtsys::examples::wfp::tls_runtime;
using namespace crtsys::wfp_sample;

constexpr std::wstring_view server_name = L"kernel.example";
constexpr std::string_view server_name_ascii = "kernel.example";

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      (void)::CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_;
};

void configure_certificate(HANDLE device,
                           const installed_machine_certificate &certificate) {
  static_assert(installed_machine_certificate::sha1_size ==
                contract::certificate_thumbprint_size);
  contract::certificate_config configuration{};
  configuration.sha1_thumbprint = certificate.thumbprint();
  configuration.server_name_size =
      static_cast<std::uint32_t>(server_name_ascii.size());
  std::memcpy(configuration.server_name.data(), server_name_ascii.data(),
              server_name_ascii.size());
  configuration.server_name[server_name_ascii.size()] = '\0';
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::configure_certificate_ioctl,
                         &configuration, sizeof(configuration), nullptr, 0,
                         &bytes, nullptr))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(configure certificate)");
}

contract::proxy_info query_proxy(HANDLE device) {
  contract::proxy_info info{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::query_proxy_ioctl, nullptr, 0,
                         &info, sizeof(info), &bytes, nullptr) ||
      bytes != sizeof(info))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(query TLS proxy)");
  return info;
}

contract::inspection_record query_last_inspection(HANDLE device) {
  contract::inspection_record record{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::query_last_inspection_ioctl,
                         nullptr, 0, &record, sizeof(record), &bytes,
                         nullptr) ||
      bytes != sizeof(record))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(query last inspection)");
  return record;
}

contract::inspection_read_result read_inspection(HANDLE device,
                                                  std::uint64_t after) {
  contract::inspection_cursor cursor{after};
  contract::inspection_read_result result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(device, contract::read_inspection_ioctl, &cursor,
                         sizeof(cursor), &result, sizeof(result), &bytes,
                         nullptr) ||
      bytes != sizeof(result))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(read inspection)");
  return result;
}

void install_policy(ntl::wfp::policy_session &session,
                    const contract::proxy_info &proxy,
                    std::uint16_t original_port_v4,
                    std::uint16_t original_port_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key, L"crtsys kernel TLS inspection provider",
         L"Redirects selected TLS clients to kernel Schannel"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key, L"crtsys kernel TLS inspection sublayer",
         L"Bounded dual-stack two-leg TLS inspection policy", 0x7630});
    const auto callout_v4 = transaction.add_callout<contract::layer_v4>(
        provider, {contract::callout_key_v4,
                   L"Redirect IPv4 TLS to kernel Schannel", L""});
    const auto callout_v6 = transaction.add_callout<contract::layer_v6>(
        provider, {contract::callout_key_v6,
                   L"Redirect IPv6 TLS to kernel Schannel", L""});
    ntl::wfp::connect_redirect_filter_builder<contract::layer_v4> filter_v4(
        contract::filter_key_v4, L"Inspect selected IPv4 TLS connection",
        {proxy.process_id, proxy.port_v4,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v4.protocol_equal(IPPROTO_TCP).remote_port_equal(original_port_v4);
    transaction.add_connect_redirect_filter(sublayer, callout_v4, filter_v4);
    ntl::wfp::connect_redirect_filter_builder<contract::layer_v6> filter_v6(
        contract::filter_key_v6, L"Inspect selected IPv6 TLS connection",
        {proxy.process_id, proxy.port_v6,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v6.protocol_equal(IPPROTO_TCP).remote_port_equal(original_port_v6);
    transaction.add_connect_redirect_filter(sublayer, callout_v6, filter_v6);
  });
}

struct capture_summary {
  struct failure {
    std::uint64_t session_id = 0;
    std::int32_t status = 0;
  };

  std::uint64_t records = 0;
  std::uint64_t permitted = 0;
  std::uint64_t blocked = 0;
  std::uint64_t failed = 0;
  std::uint64_t ipv4 = 0;
  std::uint64_t ipv6 = 0;
  std::uint64_t http1 = 0;
  std::uint64_t http2 = 0;
  std::uint64_t request_transformed = 0;
  std::uint64_t response_transformed = 0;
  std::uint64_t wrong_port = 0;
  std::uint64_t wrong_sni = 0;
  std::uint64_t invalid_failure_status = 0;
  std::uint64_t dropped = 0;
  std::vector<failure> failures;
};

capture_summary collect_capture(HANDLE device, std::uint64_t baseline,
                                std::uint16_t port_v4,
                                std::uint16_t port_v6) {
  capture_summary summary;
  std::uint64_t cursor = baseline;
  for (;;) {
    const auto next = read_inspection(device, cursor);
    summary.dropped =
        (std::max)(summary.dropped,
                   static_cast<std::uint64_t>(next.dropped));
    if (!next.available)
      return summary;
    const auto &record = next.record;
    cursor = record.sequence;
    ++summary.records;
    if (record.server_name_size != 0 &&
        std::string_view(record.server_name.data(), record.server_name_size) !=
            server_name_ascii)
      ++summary.wrong_sni;
    if (record.original_family == AF_INET) {
      ++summary.ipv4;
      if (record.original_port != port_v4)
        ++summary.wrong_port;
    } else if (record.original_family == AF_INET6) {
      ++summary.ipv6;
      if (record.original_port != port_v6)
        ++summary.wrong_port;
    }
    if (record.protocol == contract::inspected_protocol::http1)
      ++summary.http1;
    if (record.protocol == contract::inspected_protocol::http2)
      ++summary.http2;
    if ((record.flags & contract::request_transformed) != 0)
      ++summary.request_transformed;
    if ((record.flags & contract::response_transformed) != 0)
      ++summary.response_transformed;
    if (record.action == contract::inspection_action::permitted)
      ++summary.permitted;
    else if (record.action == contract::inspection_action::blocked)
      ++summary.blocked;
    else if (record.action == contract::inspection_action::failed) {
      ++summary.failed;
      summary.failures.push_back(
          {record.session_id, record.failure_status});
      if (record.failure_status >= 0)
        ++summary.invalid_failure_status;
    }
  }
}

std::uint64_t delta(std::uint64_t after, std::uint64_t before,
                    std::uint64_t &regressions) {
  if (after < before) {
    ++regressions;
    return 0;
  }
  return after - before;
}

std::string stats_text(const contract::proxy_info &before,
                       const contract::proxy_info &after,
                       const capture_summary &capture) {
  std::uint64_t regressions = 0;
  std::ostringstream output;
  output << "delta.accepted="
         << delta(after.accepted, before.accepted, regressions) << '\n'
         << "delta.handshaken="
         << delta(after.handshaken, before.handshaken, regressions) << '\n'
         << "delta.permitted="
         << delta(after.permitted, before.permitted, regressions) << '\n'
         << "delta.blocked="
         << delta(after.blocked, before.blocked, regressions) << '\n'
         << "delta.origin_connected="
         << delta(after.origin_connected, before.origin_connected,
                  regressions)
         << '\n'
         << "delta.origin_completed="
         << delta(after.origin_completed, before.origin_completed,
                  regressions)
         << '\n'
         << "delta.failed="
         << delta(after.failed, before.failed, regressions) << '\n'
         << "delta.capture_overwritten="
         << delta(after.capture_overwritten, before.capture_overwritten,
                  regressions)
         << '\n'
         << "counter_regressions=" << regressions << '\n'
         << "capture.records=" << capture.records << '\n'
         << "capture.permitted=" << capture.permitted << '\n'
         << "capture.blocked=" << capture.blocked << '\n'
         << "capture.failed=" << capture.failed << '\n'
         << "capture.ipv4=" << capture.ipv4 << '\n'
         << "capture.ipv6=" << capture.ipv6 << '\n'
         << "capture.http1=" << capture.http1 << '\n'
         << "capture.http2=" << capture.http2 << '\n'
         << "capture.request_transformed="
         << capture.request_transformed << '\n'
         << "capture.response_transformed="
         << capture.response_transformed << '\n'
         << "capture.wrong_port=" << capture.wrong_port << '\n'
         << "capture.wrong_sni=" << capture.wrong_sni << '\n'
         << "capture.invalid_failure_status="
         << capture.invalid_failure_status << '\n'
         << "capture.dropped=" << capture.dropped << '\n'
         << "credentials_ready=" << (after.credentials_ready ? 1 : 0)
         << '\n'
         << "identity_count=" << after.identity_count << '\n'
         << "policy_removed=1\n";
  for (std::size_t index = 0; index != capture.failures.size(); ++index) {
    output << "capture.failure." << index << ".session="
           << capture.failures[index].session_id << '\n'
           << "capture.failure." << index << ".status="
           << capture.failures[index].status << '\n';
  }
  return output.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    runtime::arguments arguments(argc, argv);
    const auto original_v4 = arguments.required_port(L"--ipv4-port");
    const auto original_v6 = arguments.required_port(L"--ipv6-port");
    const auto lifecycle = tls_runtime::parse_lifecycle(arguments);
    arguments.finish();

    handle_owner device(::CreateFileW(
        contract::user_device_path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw_windows("open kernel TLS inspection device");

    ephemeral_certificate authority(true);
    ntl::net::windows_tls_certificate_issuer issuer(
        authority.get(),
        {.key_name_prefix = L"crtsys-kernel-tls-inspection",
         .rsa_bits = 2048,
         .validity_days = 1,
         .machine_keys = true});
    auto issued_leaf = issuer.issue(server_name);
    installed_machine_certificate trusted_root(authority.get(), L"ROOT");
    installed_machine_certificate installed_leaf(
        issued_leaf.borrowed_certificate(), L"MY");
    configure_certificate(device.get(), installed_leaf);
    const auto &identity_thumbprint = installed_leaf.thumbprint();
    runtime::write_file(
        lifecycle.identity_thumbprint_file,
        {reinterpret_cast<const char *>(identity_thumbprint.data()),
         identity_thumbprint.size()});
    const auto before = query_proxy(device.get());
    if (!before.credentials_ready || !before.process_id || !before.port_v4 ||
        !before.port_v6 || before.identity_count != 1)
      throw std::runtime_error("kernel TLS proxy is not ready");
    const auto capture_baseline =
        query_last_inspection(device.get()).sequence;
    if (!lifecycle.ca_file.parent_path().empty())
      std::filesystem::create_directories(lifecycle.ca_file.parent_path());
    authority.export_public_certificate(lifecycle.ca_file);

    contract::proxy_info after{};
    capture_summary capture;
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys kernel TLS proxy controller");
      install_policy(policy, before, original_v4, original_v6);
      tls_runtime::signal_ready(lifecycle);
      tls_runtime::wait_for_file(
          lifecycle.remove_policy_file, lifecycle.duration_ms,
          [&] { (void)query_proxy(device.get()); });
      after = query_proxy(device.get());
      capture = collect_capture(device.get(), capture_baseline, original_v4,
                                original_v6);
    }
    tls_runtime::signal_policy_removed(lifecycle);
    tls_runtime::wait_for_file(lifecycle.stop_file, lifecycle.duration_ms,
                               [] {});
    runtime::write_file(lifecycle.stats_file,
                        stats_text(before, after, capture));
    std::wcout
        << L"Kernel TLS controller stopped; policy and test identities "
           L"removed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel TLS controller failed: " << error.what() << '\n';
    return 1;
  }
}
