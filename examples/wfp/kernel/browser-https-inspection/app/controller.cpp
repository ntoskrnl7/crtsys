#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "controller.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <ntl/wfp/management>
#include <ntl/wfp/telemetry>

#include "browser_policy.hpp"
#include "capture_log.hpp"
#include "certificate_authority.hpp"
#include "certificate_store.hpp"
#include "identity_provisioner.hpp"
#include "kernel_tls_service.hpp"

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

std::atomic<bool> stop_requested{false};

BOOL WINAPI console_control(DWORD control) {
  if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT ||
      control == CTRL_CLOSE_EVENT || control == CTRL_SHUTDOWN_EVENT) {
    stop_requested.store(true, std::memory_order_release);
    return TRUE;
  }
  return FALSE;
}

class console_handler {
public:
  console_handler() {
    stop_requested.store(false, std::memory_order_release);
    if (!::SetConsoleCtrlHandler(console_control, TRUE))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "SetConsoleCtrlHandler(kernel browser)");
  }
  console_handler(const console_handler &) = delete;
  console_handler &operator=(const console_handler &) = delete;
  ~console_handler() { (void)::SetConsoleCtrlHandler(console_control, FALSE); }
};

std::string identity_name(const contract::identity_request &request) {
  if (request.server_name_size == 0 ||
      request.server_name_size > contract::maximum_server_name_size)
    throw std::runtime_error("kernel browser identity request is invalid");
  return std::string(request.server_name.data(), request.server_name_size);
}

void require_service_ready(const contract::service_info &service) {
  if (!service.process_id || !service.tcp_ready ||
      !service.workspace_lifetime_passed || !service.tcp_port_v4 ||
      !service.tcp_port_v6 ||
      service.identity_capacity != contract::identity_cache_capacity)
    throw std::runtime_error("kernel browser inspection service is not ready");
}

struct native_drop_observation {
  bool ipv4 = false;
  bool ipv6 = false;
};

void collect_native_drop_events(
    ntl::wfp::network_event_monitor &monitor,
    const native_quic_policy_evidence &policy,
    native_drop_observation &observed) {
  ntl::wfp::network_event_snapshot event{};
  while (monitor.try_pop(event)) {
    if (event.kind != ntl::wfp::network_event_kind::classify_drop ||
        event.protocol != IPPROTO_UDP || event.remote_port != 443 ||
        event.application_id_hash != policy.application_id_hash)
      continue;

    const char *layer = nullptr;
    bool *already_reported = nullptr;
    if (event.filter_id == policy.filter_id_v4 &&
        event.layer_id == policy.layer_id_v4) {
      layer = "ALE_AUTH_CONNECT_V4";
      already_reported = &observed.ipv4;
    } else if (event.filter_id == policy.filter_id_v6 &&
               event.layer_id == policy.layer_id_v6) {
      layer = "ALE_AUTH_CONNECT_V6";
      already_reported = &observed.ipv6;
    }
    if (!layer || *already_reported)
      continue;
    *already_reported = true;
    std::cout
        << "NTL WFP native UDP/443 drop event: observed "
           "kind=classify-drop layer="
        << layer << " filter_id=" << event.filter_id
        << " protocol=17 remote_port=443 application_scoped=true\n";
  }
}

} // namespace

int run_controller(const std::filesystem::path &browser_argument,
                   const std::filesystem::path &log_directory_argument,
                   std::uint32_t duration_seconds) {
  const auto browser = std::filesystem::canonical(browser_argument);
  if (!std::filesystem::is_regular_file(browser))
    throw std::invalid_argument("browser target is not a regular file");
  capture_log logger(log_directory_argument);
  const auto stop_file = logger.root() / "stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_file, ignored);
  }

  device_handle device;
  const auto before = query_service(device.get());
  require_service_ready(before);
  ephemeral_authority authority;
  authority.export_public_certificate(logger.root() / "inspection-ca.cer");
  installed_certificate trusted_root(authority.get(), L"ROOT");
  identity_provisioner identities(
      [&device](const contract::certificate_config &identity) {
        configure_identity(device.get(), identity);
      },
      authority.get(), logger.root());
  const auto browser_id =
      ntl::wfp::application_id::from_path(browser.wstring());
  ntl::wfp::network_event_monitor network_events(
      {.maximum_queued_events = 1024, .manage_collection_state = true});
  auto policy = ntl::wfp::policy_session::ephemeral(
      L"crtsys kernel browser HTTPS continuous inspection");
  const auto policy_evidence =
      install_browser_policy(policy, browser_id, before);
  report_browser_policy_evidence(policy_evidence, logger.root());
  native_drop_observation observed_drops{};
  console_handler controls;

  std::uint64_t identity_sequence = 0;
  std::uint64_t inspection_sequence = 0;
  std::uint64_t records = 0;
  std::uint64_t dropped = 0;
  const auto started = std::chrono::steady_clock::now();
  const auto deadline =
      duration_seconds == 0
          ? std::chrono::steady_clock::time_point::max()
          : started + std::chrono::seconds(duration_seconds);

  std::wcout
      << L"Kernel browser HTTPS inspection is active for exactly:\n  "
      << browser.wstring()
      << L"\nUse that browser normally. No browser process is launched and no "
         L"browser setting, policy, profile, certificate-error switch, or "
         L"protocol feature flag is changed. Press Ctrl+C or create "
      << stop_file.wstring() << L" to stop.\n";

  while (!stop_requested.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline &&
         !std::filesystem::exists(stop_file)) {
    collect_native_drop_events(network_events, policy_evidence,
                               observed_drops);
    for (;;) {
      const auto next = read_identity_request(device.get(), identity_sequence);
      dropped += next.dropped;
      if (!next.available)
        break;
      identities.ensure(identity_name(next.request));
      identity_sequence = next.request.sequence;
    }
    for (;;) {
      const auto next = read_inspection(device.get(), inspection_sequence);
      dropped += next.dropped;
      if (!next.available)
        break;
      logger.write(next.record);
      inspection_sequence = next.record.sequence;
      ++records;
      std::cout << "kernel HTTPS capture: sequence=" << next.record.sequence
                << " session=" << next.record.session_id
                << " protocol="
                << static_cast<unsigned>(next.record.protocol)
                << " status=" << next.record.status << '\n';
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  collect_native_drop_events(network_events, policy_evidence,
                             observed_drops);

  for (;;) {
    const auto next = read_inspection(device.get(), inspection_sequence);
    dropped += next.dropped;
    if (!next.available)
      break;
    logger.write(next.record);
    inspection_sequence = next.record.sequence;
    ++records;
  }
  const auto after = query_service(device.get());
  logger.write_summary(before, after, records, dropped);
  std::wcout << L"Kernel browser HTTPS inspection stopped: records=" << records
             << L", dropped=" << dropped << L", captures="
             << logger.root().wstring()
             << L". WFP policy and temporary certificate identities are "
                L"removed when this process exits.\n";
  return dropped == 0 ? 0 : 2;
}

} // namespace crtsys::wfp_kernel_browser_https
