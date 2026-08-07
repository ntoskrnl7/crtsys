#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include "browser_runtime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <syncstream>
#include <utility>
#include <vector>

#include <ntl/net/io/async_socket>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/tls/product_backend>
#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/transform>
#include <ntl/net/grpc/transform>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/inspection_frontend>
#include <ntl/net/tls/inspection_policy>
#include <ntl/net/tls/stream>
#include <ntl/net/user/structured_concurrency>
#include <ntl/handle>
#include <ntl/wfp/connect_redirect>
#include <ntl/wfp/management>
#include <ntl/wfp/telemetry>

#include "browser_https_inspection_contract.hpp"
#include "browser_http_policy.hpp"
#include "browser_log.hpp"
#include "browser_policy.hpp"
#include "browser_policy_diagnostics.hpp"
#include "browser_proxy.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

struct native_drop_observation {
  bool ipv4 = false;
  bool ipv6 = false;
};

void collect_native_drop_events(
    ntl::wfp::network_event_monitor &monitor,
    const browser_policy_diagnostic_summary &policy,
    native_drop_observation &observed,
    browser_html_logger &logger) {
  ntl::wfp::network_event_snapshot event{};
  while (monitor.try_pop(event)) {
    if (event.kind != ntl::wfp::network_event_kind::classify_drop ||
        event.protocol != IPPROTO_UDP || event.remote_port != 443 ||
        event.application_id_hash != policy.application_id_hash)
      continue;

    const char *layer = nullptr;
    bool *already_reported = nullptr;
    if (event.filter_id == policy.native_filter_id_v4 &&
        event.layer_id == policy.native_layer_id_v4) {
      layer = "ALE_AUTH_CONNECT_V4";
      already_reported = &observed.ipv4;
    } else if (event.filter_id == policy.native_filter_id_v6 &&
               event.layer_id == policy.native_layer_id_v6) {
      layer = "ALE_AUTH_CONNECT_V6";
      already_reported = &observed.ipv6;
    }
    if (!layer || *already_reported)
      continue;
    *already_reported = true;
    const std::string marker =
        "NTL WFP native UDP/443 drop event: observed "
        "kind=classify-drop layer=" +
        std::string(layer) + " filter_id=" +
        std::to_string(event.filter_id) +
        " protocol=17 remote_port=443 application_scoped=true";
    logger.record_lifecycle(marker);
    std::cout << marker << '\n';
  }
}

ntl::unique_handle open_quic_telemetry() {
  ntl::unique_handle device(::CreateFileW(
      wfp_browser_https_inspection::user_device_path,
      GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, 0, nullptr));
  if (!device)
    throw_windows(
        "CreateFileW(browser QUIC telemetry)");
  return device;
}

wfp_browser_https_inspection::quic_telemetry
query_quic_telemetry(HANDLE device) {
  wfp_browser_https_inspection::quic_telemetry result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(
          device,
          wfp_browser_https_inspection::query_telemetry_ioctl,
          nullptr, 0, &result, sizeof(result), &bytes, nullptr))
    throw_windows(
        "DeviceIoControl(browser QUIC telemetry)");
  if (bytes != sizeof(result) ||
      result.version !=
          wfp_browser_https_inspection::telemetry_version ||
      result.size != sizeof(result))
    throw std::runtime_error(
        "browser QUIC telemetry ABI mismatch");
  return result;
}

wfp_browser_https_inspection::quic_layer_telemetry
quic_telemetry_delta(
    const wfp_browser_https_inspection::quic_layer_telemetry &before,
    const wfp_browser_https_inspection::quic_layer_telemetry &after) {
  auto result = after;
  result.classify_hits -= before.classify_hits;
  result.block_decisions -= before.block_decisions;
  result.action_write_available -=
      before.action_write_available;
  result.action_write_missing -=
      before.action_write_missing;
  result.initial_permit -= before.initial_permit;
  return result;
}

wfp_browser_https_inspection::udp_translation_telemetry
udp_translation_delta(
    const wfp_browser_https_inspection::udp_translation_telemetry &before,
    const wfp_browser_https_inspection::udp_translation_telemetry &after) {
  return {
      after.outbound_packets - before.outbound_packets,
      after.inbound_packets - before.inbound_packets,
      after.mapping_updates - before.mapping_updates,
      after.mapping_misses - before.mapping_misses,
      after.injection_failures - before.injection_failures,
      after.quota_rejections - before.quota_rejections};
}

std::string format_quic_layer_telemetry(
    std::string_view name,
    const wfp_browser_https_inspection::quic_layer_telemetry &value,
    std::uint64_t expected_application_id_hash,
    std::size_t expected_application_id_size) {
  const bool application_matches =
      value.classify_hits != 0 &&
      value.last_application_id_hash ==
          expected_application_id_hash &&
      value.last_application_id_size ==
          expected_application_id_size;
  std::ostringstream output;
  output << name
         << "-hits=" << value.classify_hits
         << ' ' << name
         << "-blocks=" << value.block_decisions
         << ' ' << name
         << "-action-write=" << value.action_write_available
         << ' ' << name
         << "-action-write-missing=" << value.action_write_missing
         << ' ' << name
         << "-initial-permit=" << value.initial_permit
         << ' ' << name
         << "-last-pid=" << value.last_process_id
         << ' ' << name
         << "-last-protocol="
         << static_cast<unsigned>(value.last_protocol)
         << ' ' << name
         << "-last-port=" << value.last_remote_port
         << ' ' << name
         << "-app-id-match="
         << (application_matches ? "yes" : "no")
         << ' ' << name
         << "-filter-id=" << value.last_filter_id
         << ' ' << name
         << "-filter-flags=" << value.last_filter_flags;
  return output.str();
}

class schannel_browser_identity_provider final
    : public ntl::net::tls_server_identity_provider {
public:
  explicit schannel_browser_identity_provider(
      std::shared_ptr<ntl::net::inspection::managed_tls_frontend> frontend,
      std::span<const std::uint8_t> application_id)
      : frontend_(std::move(frontend)) {
    application_id_.reserve(application_id.size());
    for (const std::uint8_t value : application_id)
      application_id_.push_back(
          static_cast<std::byte>(value));
  }

  std::shared_ptr<ntl::net::tls_server_identity>
  select(const ntl::net::tls_client_hello &hello) override {
    auto selected = frontend_->select(hello, application_id_);
    if (!selected)
      throw std::system_error(
          static_cast<int>(
              static_cast<NTSTATUS>(selected.status())),
          std::system_category(),
          "managed TLS frontend");
    if (selected->transport !=
        ntl::net::inspection::frontend_transport_kind::schannel)
      throw std::system_error(
          ERROR_NOT_SUPPORTED,
          std::system_category(),
          "decrypted ECH requires a frontend-owned TLS stream; "
          "Schannel cannot resume from an inner ClientHello");
    return selected->identity;
  }

private:
  std::shared_ptr<ntl::net::inspection::managed_tls_frontend> frontend_;
  std::vector<std::byte> application_id_;
};

std::atomic<bool> browser_inspection_stop{false};

class browser_connection_completion {
public:
  std::size_t snapshot() const noexcept {
    std::lock_guard lock(lock_);
    return generation_;
  }

  void notify() noexcept {
    {
      std::lock_guard lock(lock_);
      ++generation_;
    }
    changed_.notify_all();
  }

  void wait_for_change(std::size_t observed) noexcept {
    std::unique_lock lock(lock_);
    changed_.wait(
        lock, [this, observed] {
          return generation_ != observed;
        });
  }

private:
  mutable std::mutex lock_;
  std::condition_variable changed_;
  std::size_t generation_ = 0;
};

class browser_connection final
    : public std::enable_shared_from_this<browser_connection> {
public:
  browser_connection(
      std::shared_ptr<ntl::net::user::redirected_tls_session> session,
      std::shared_ptr<browser_html_logger> logger,
      std::shared_ptr<browser_connection_completion> completion) noexcept
      : session_(std::move(session)), logger_(std::move(logger)),
        completion_(std::move(completion)) {}

  browser_connection(const browser_connection &) = delete;
  browser_connection &operator=(const browser_connection &) = delete;

  void start() {
    if (started_)
      throw std::logic_error("browser connection task started twice");
    started_ = true;
    task_.emplace(ntl::net::user::start_background(
        run(shared_from_this()),
        [weak = weak_from_this()]() noexcept {
          if (auto self = weak.lock())
            self->stop();
        }));
  }

  void stop() noexcept {
    if (session_)
      session_->cancel();
  }

  bool finished() const noexcept {
    return finished_.load(std::memory_order_acquire);
  }

private:
  static ntl::net::user::task<unsigned> run(
      std::shared_ptr<browser_connection> self) {
    try {
      (void)co_await self->session_->run();
    } catch (const std::exception &error) {
      if (!browser_inspection_stop.load(std::memory_order_acquire)) {
        self->logger_->record_error(error.what());
        std::osyncstream(std::cerr)
            << "Browser HTTPS connection closed: " << error.what() << '\n';
      }
    } catch (...) {
      if (!browser_inspection_stop.load(std::memory_order_acquire)) {
        self->logger_->record_error(
            "unknown browser HTTPS connection failure");
        std::osyncstream(std::cerr)
            << "Browser HTTPS connection closed: unknown failure\n";
      }
    }
    self->stop();
    self->finished_.store(true, std::memory_order_release);
    self->completion_->notify();
    co_return 0;
  }

  std::shared_ptr<ntl::net::user::redirected_tls_session> session_;
  std::shared_ptr<browser_html_logger> logger_;
  std::shared_ptr<browser_connection_completion> completion_;
  std::optional<ntl::net::user::background_operation<unsigned>> task_;
  std::atomic<bool> finished_{false};
  bool started_ = false;
};

class browser_connection_registry {
public:
  browser_connection_registry() = default;
  browser_connection_registry(const browser_connection_registry &) = delete;
  browser_connection_registry &
  operator=(const browser_connection_registry &) = delete;

  ~browser_connection_registry() {
    stop_all();
    wait_for_all();
  }

  void start(
      ntl::net::io_completion_context &context, socket_owner inbound,
      std::shared_ptr<ntl::net::tls_server_identity_provider> identities,
      std::shared_ptr<ntl::net::user::redirected_tls_http_dispatcher>
          dispatcher,
      std::shared_ptr<ntl::net::inspection::origin_client_identity_provider>
          origin_identities,
      std::shared_ptr<browser_html_logger> logger) {
    auto session = ntl::net::user::redirected_tls_session::create(
        context, inbound.release(), std::move(identities),
        std::move(dispatcher),
        {.client_identities = std::move(origin_identities)}, {}, {},
        "browser");
    auto connection = std::make_shared<browser_connection>(
        std::move(session), std::move(logger), completion_);
    connections_.push_back(connection);
    try {
      connection->start();
    } catch (...) {
      connections_.pop_back();
      throw;
    }
  }

  void reap_finished() noexcept {
    for (auto current = connections_.begin();
         current != connections_.end();) {
      if ((*current)->finished())
        current = connections_.erase(current);
      else
        ++current;
    }
  }

  void stop_all() noexcept {
    for (const auto &connection : connections_)
      connection->stop();
  }

  void wait_for_all() noexcept {
    while (!connections_.empty()) {
      const std::size_t observed = completion_->snapshot();
      reap_finished();
      if (!connections_.empty())
        completion_->wait_for_change(observed);
    }
  }

  std::size_t size() const noexcept { return connections_.size(); }

private:
  std::shared_ptr<browser_connection_completion> completion_ =
      std::make_shared<browser_connection_completion>();
  std::vector<std::shared_ptr<browser_connection>> connections_;
};

BOOL WINAPI browser_console_control(DWORD control) {
  if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT ||
      control == CTRL_CLOSE_EVENT ||
      control == CTRL_SHUTDOWN_EVENT) {
    browser_inspection_stop.store(true, std::memory_order_release);
    return TRUE;
  }
  return FALSE;
}

} // namespace

int run_browser_inspection(
    const std::filesystem::path &browser_argument,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds,
    browser_inspection_security_providers providers) {
  const auto browser = std::filesystem::canonical(browser_argument);
  if (!std::filesystem::is_regular_file(browser))
    throw std::invalid_argument(
        "browser inspection target is not a regular file");
  const auto log_directory =
      std::filesystem::absolute(log_argument);
  auto logger = std::make_shared<browser_html_logger>(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate;
  const auto certificate_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate.export_public_certificate(certificate_path);
  auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
      certificate.get(),
      ntl::net::windows_tls_certificate_issuer_options{
          .key_name_prefix = L"crtsys-ntl-wfp-browser",
          .rsa_bits = 2048,
          .validity_days = 2,
          .machine_keys = true});
  const auto browser_id =
      ntl::wfp::application_id::from_path(browser.wstring());
  const auto expected_application_id_hash =
      wfp_browser_https_inspection::hash_application_id(
          browser_id.bytes().data(), browser_id.bytes().size());
  auto telemetry_device = open_quic_telemetry();
  const auto initial_telemetry =
      query_quic_telemetry(telemetry_device.get());
  auto ech = providers.ech
                 ? std::move(providers.ech)
                 : std::make_shared<
                       ntl::net::inspection::unavailable_ech_frontend>();
  auto downstream_trust =
      providers.downstream_trust
          ? std::move(providers.downstream_trust)
          : std::make_shared<
                ntl::net::inspection::inspectable_downstream_trust>();
  auto origin_identities =
      providers.origin_client_identity
          ? std::move(providers.origin_client_identity)
          : std::make_shared<
                ntl::net::inspection::unavailable_origin_client_identity>();
  auto identities =
      std::make_shared<ntl::net::cached_tls_server_identity_provider>(
          issuer, 256);
  auto tls_audit =
      std::make_shared<ntl::net::inspection::bounded_tls_audit_sink>(1024);
  auto tls_frontend =
      std::make_shared<ntl::net::inspection::managed_tls_frontend>(
          identities, ech, downstream_trust, tls_audit);
  auto inspectable_identities =
      std::make_shared<schannel_browser_identity_provider>(
          tls_frontend, browser_id.bytes());
  auto audited_origin_identities = std::make_shared<
      ntl::net::inspection::audited_origin_client_identity_provider>(
      origin_identities, tls_audit);
  auto http_policy = crtsys::wfp_browser_http_policy::
      make_browser_inspection_policy();
  auto http_dispatcher = make_browser_http_dispatcher(
      std::move(http_policy), logger);
  auto proxy_listener_v4 = make_listener();
  auto proxy_listener_v6 = make_ipv6_listener();
  ntl::wfp::network_event_monitor network_events(
      {.maximum_queued_events = 1024,
       .manage_collection_state = true});
  std::optional<ntl::wfp::policy_session> policy;
  policy.emplace(ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp browser HTTPS inspection"));
  install_browser_policy(
      *policy, browser_id, proxy_listener_v4.port,
      proxy_listener_v6.port);
  const auto policy_diagnostics =
      verify_browser_quic_block_policy(
          *policy, browser_id, log_directory);
  const std::string policy_diagnostic_message =
      "verified ipv4-filters=" +
      std::to_string(policy_diagnostics.ipv4_filter_count) +
      " ipv4-truncated=" +
      (policy_diagnostics.ipv4_inventory_truncated ? "yes" : "no") +
      " ipv6-filters=" +
      std::to_string(policy_diagnostics.ipv6_filter_count) +
      " ipv6-truncated=" +
      (policy_diagnostics.ipv6_inventory_truncated ? "yes" : "no") +
      " report=" +
      policy_diagnostics.report_path.filename().string();
  logger->record_lifecycle(
      "wfp-policy " + policy_diagnostic_message);
  std::cout << "NTL WFP policy diagnostics: "
            << policy_diagnostic_message << '\n';
  std::cout
      << "NTL WFP native UDP/443 block: verified "
         "kind=native-enforcement layer=ALE_AUTH_CONNECT_V4 "
         "action=FWP_ACTION_BLOCK protocol=UDP remote_port=443 "
         "application_scoped=true filter_id="
      << policy_diagnostics.native_filter_id_v4 << '\n'
      << "NTL WFP native UDP/443 block: verified "
         "kind=native-enforcement layer=ALE_AUTH_CONNECT_V6 "
         "action=FWP_ACTION_BLOCK protocol=UDP remote_port=443 "
         "application_scoped=true filter_id="
      << policy_diagnostics.native_filter_id_v6 << '\n';
  native_drop_observation observed_native_drops{};

  browser_inspection_stop.store(false, std::memory_order_release);
  if (!::SetConsoleCtrlHandler(&browser_console_control, TRUE))
    throw_windows("SetConsoleCtrlHandler(browser inspection)");
  struct console_handler_guard {
    ~console_handler_guard() {
      (void)::SetConsoleCtrlHandler(
          &browser_console_control, FALSE);
    }
  } handler_guard;

  std::wcout
      << L"NTL WFP browser HTTPS inspection ready: browser="
      << browser.wstring() << L", proxy-port="
      << proxy_listener_v4.port << L", proxy-port-v6="
      << proxy_listener_v6.port << L", quic-policy="
      << L"blocked-for-tcp-fallback"
      << L", http3-port=0"
      << L", ca="
      << certificate_path.wstring() << L", logs="
      << log_directory.wstring() << L'\n';

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  ntl::net::io_completion_context connection_io;
  browser_connection_registry connections;
  while (!browser_inspection_stop.load(
             std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::error_code stop_error;
    const bool stop_file =
        std::filesystem::exists(stop_path, stop_error);
    if (stop_error) {
      logger->record_error(
          "cannot query browser inspection stop file");
      break;
    }
    if (stop_file)
      break;

    collect_native_drop_events(
        network_events, policy_diagnostics,
        observed_native_drops, *logger);
    connections.reap_finished();

    listener *ready_listener = nullptr;
    if (has_pending_connection(proxy_listener_v4))
      ready_listener = &proxy_listener_v4;
    else if (has_pending_connection(proxy_listener_v6))
      ready_listener = &proxy_listener_v6;
    if (!ready_listener)
      continue;
    auto inbound = accept_one(*ready_listener);
    try {
      connections.start(
          connection_io, std::move(inbound), inspectable_identities,
          http_dispatcher, audited_origin_identities, logger);
    } catch (const std::exception &error) {
      logger->record_error(error.what());
      std::osyncstream(std::cerr)
          << "Browser HTTPS accept failed: "
          << error.what() << '\n';
    }
  }

  collect_native_drop_events(
      network_events, policy_diagnostics,
      observed_native_drops, *logger);

  browser_inspection_stop.store(true, std::memory_order_release);
  logger->record_lifecycle(
      "shutdown-begin connections=" +
      std::to_string(connections.size()));
  proxy_listener_v4.socket.reset();
  proxy_listener_v6.socket.reset();
  connections.stop_all();
  logger->record_lifecycle("connection-tasks-cancelled");
  connections.wait_for_all();
  connection_io.wait_for_idle();
  logger->record_lifecycle("connection-tasks-drained");
  logger->record_lifecycle(
      "tls-audit events=" +
      std::to_string(tls_audit->snapshot().size()) +
      " discarded=" + std::to_string(tls_audit->discarded()));

  const auto final_telemetry =
      query_quic_telemetry(telemetry_device.get());
  const auto ipv4_telemetry =
      quic_telemetry_delta(
          initial_telemetry.ipv4,
          final_telemetry.ipv4);
  const auto ipv6_telemetry =
      quic_telemetry_delta(
          initial_telemetry.ipv6,
          final_telemetry.ipv6);
  const auto translation = udp_translation_delta(
      initial_telemetry.translation, final_telemetry.translation);
  const std::string telemetry_message =
      "quic-telemetry expected-app-id-hash=" +
      std::to_string(expected_application_id_hash) + " " +
      format_quic_layer_telemetry(
          "ipv4", ipv4_telemetry,
          expected_application_id_hash,
          browser_id.bytes().size()) + " " +
      format_quic_layer_telemetry(
          "ipv6", ipv6_telemetry,
          expected_application_id_hash,
          browser_id.bytes().size()) +
      " udp-outbound=" + std::to_string(translation.outbound_packets) +
      " udp-inbound=" + std::to_string(translation.inbound_packets) +
      " udp-mapping-updates=" +
          std::to_string(translation.mapping_updates) +
      " udp-mapping-misses=" +
          std::to_string(translation.mapping_misses) +
      " udp-injection-failures=" +
          std::to_string(translation.injection_failures) +
      " udp-quota-rejections=" +
          std::to_string(translation.quota_rejections);
  logger->record_lifecycle(telemetry_message);
  std::cout << "NTL WFP QUIC telemetry: "
            << telemetry_message << '\n';
  policy.reset();
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  std::cout
      << "NTL WFP browser HTTPS inspection stopped: html-files="
      << logger->html_files()
      << ", http3-delivered=0"
      << ", policy=removed, application-trust-store-writes=none\n";
  return 0;
}

} // namespace crtsys::wfp_sample::browser_https
