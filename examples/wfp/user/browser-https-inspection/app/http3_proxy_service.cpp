#include "http3_proxy_service.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <ntl/net/tls/certificate>
#include <ntl/handle>

#include "browser_log.hpp"
#include "browser_https_inspection_contract.hpp"
#include "browser_policy.hpp"
#include "http3_live_proxy.hpp"
#include "test_certificate.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

wfp_browser_https_inspection::quic_telemetry query_driver_telemetry() {
  ntl::unique_handle device(::CreateFileW(
      wfp_browser_https_inspection::user_device_path,
      GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, 0, nullptr));
  if (!device)
    throw std::system_error(
        ::GetLastError(), std::system_category(),
        "open browser inspection telemetry");
  wfp_browser_https_inspection::quic_telemetry result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(
          device.get(),
          wfp_browser_https_inspection::query_telemetry_ioctl,
          nullptr, 0, &result, sizeof(result), &bytes, nullptr))
    throw std::system_error(
        ::GetLastError(), std::system_category(),
        "query browser inspection telemetry");
  if (bytes != sizeof(result) ||
      result.version != wfp_browser_https_inspection::telemetry_version ||
      result.size != sizeof(result))
    throw std::runtime_error("browser inspection telemetry ABI mismatch");
  return result;
}

std::uint64_t delta(std::uint64_t before, std::uint64_t after) noexcept {
  return after - before;
}

class service_ready_signal {
public:
  explicit service_ready_signal(
      const std::filesystem::path &path)
      : path_(path) {
    std::error_code ignored;
    (void)std::filesystem::remove(path_, ignored);
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << "ready\n";
  }

  service_ready_signal(const service_ready_signal &) = delete;
  service_ready_signal &operator=(const service_ready_signal &) = delete;

  ~service_ready_signal() {
    std::error_code ignored;
    (void)std::filesystem::remove(path_, ignored);
  }

private:
  std::filesystem::path path_;
};

} // namespace

int run_managed_http3_proxy(
    std::uint16_t listen_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  if (listen_port == 0)
    throw std::invalid_argument(
        "managed HTTP/3 proxy requires a nonzero port");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  auto logger = std::make_shared<browser_html_logger>(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate_authority;
  const auto ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate_authority.export_public_certificate(ca_path);
  auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
      certificate_authority.get(),
      ntl::net::windows_tls_certificate_issuer_options{
       .key_name_prefix =
           L"crtsys-ntl-managed-http3",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .reuse_leaf_key = false});
  browser_http3_service service(
      issuer, logger, listen_port, nullptr,
      http3_origin_policy::allow_tls_tcp_fallback);
  service_ready_signal ready(
      log_directory / L"service.ready");

  std::wcout
      << L"NTL managed HTTP/3 inspection ready: "
      << L"listen=127.0.0.1:" << listen_port
      << L", ca=" << ca_path.wstring()
      << L", logs=" << log_directory.wstring()
      << L", browser-settings=unchanged, wfp-udp=not-used\n";

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error, "query managed HTTP/3 proxy stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  service.stop();
  if (!service.wait_for_drain(15))
    throw std::runtime_error(
        "managed HTTP/3 connections did not drain");
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }
  std::wcout
      << L"NTL managed HTTP/3 inspection stopped: "
      << L"delivered-requests="
      << service.delivered_requests()
      << L", html-files=" << logger->html_files()
      << L", dynamic-hosts=" << service.dynamic_hosts()
      << L", downstream=h3, upstream=recorded-per-request"
      << L", persistent-browser-changes=none\n";
  return 0;
}

int run_wfp_managed_http3_proxy(
    const std::filesystem::path &client_argument,
    std::uint16_t listen_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  if (listen_port == 0)
    throw std::invalid_argument(
        "WFP managed HTTP/3 proxy requires a nonzero port");
  const auto client =
      std::filesystem::canonical(client_argument);
  if (!std::filesystem::is_regular_file(client))
    throw std::invalid_argument(
        "managed HTTP/3 client path is not a regular file");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  auto logger = std::make_shared<browser_html_logger>(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate_authority;
  const auto ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate_authority.export_public_certificate(ca_path);
  auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
      certificate_authority.get(),
      ntl::net::windows_tls_certificate_issuer_options{
       .key_name_prefix =
           L"crtsys-ntl-wfp-managed-http3",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .reuse_leaf_key = false});
  browser_http3_service service(
      issuer, logger, listen_port, nullptr,
      http3_origin_policy::allow_tls_tcp_fallback);

  const auto initial_telemetry = query_driver_telemetry();
  const auto client_id =
      ntl::wfp::application_id::from_path(client.wstring());
  std::optional<ntl::wfp::policy_session> policy;
  policy.emplace(ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp managed HTTP/3 translation"));
  install_managed_http3_translation_policy(
      *policy, client_id, listen_port);
  service_ready_signal ready(
      log_directory / L"service.ready");

  std::wcout
      << L"NTL WFP managed HTTP/3 inspection ready: client="
      << client.wstring() << L", udp443=translated, listen="
      << listen_port << L", ca=" << ca_path.wstring()
      << L", logs=" << log_directory.wstring() << L'\n';

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error, "query WFP managed HTTP/3 stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  policy.reset();
  service.stop();
  if (!service.wait_for_drain(15))
    throw std::runtime_error(
        "WFP managed HTTP/3 connections did not drain");
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }
  const auto final_telemetry = query_driver_telemetry();
  const auto &before = initial_telemetry.translation;
  const auto &after = final_telemetry.translation;
  std::wcout
      << L"NTL WFP managed HTTP/3 inspection stopped: "
      << L"delivered-requests=" << service.delivered_requests()
      << L", html-files=" << logger->html_files()
      << L", dynamic-hosts=" << service.dynamic_hosts()
      << L", udp-outbound="
      << delta(before.outbound_packets, after.outbound_packets)
      << L", udp-inbound="
      << delta(before.inbound_packets, after.inbound_packets)
      << L", udp-mapping-updates="
      << delta(before.mapping_updates, after.mapping_updates)
      << L", udp-mapping-misses="
      << delta(before.mapping_misses, after.mapping_misses)
      << L", udp-injection-failures="
      << delta(before.injection_failures, after.injection_failures)
      << L", udp-quota-rejections="
      << delta(before.quota_rejections, after.quota_rejections)
      << L", wfp-policy=removed, persistent-trust=none\n";
  return 0;
}

} // namespace crtsys::wfp_sample::browser_https
