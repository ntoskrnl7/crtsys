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
#include <ntl/net/inspection/core>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/inspection_frontend>
#include <ntl/net/tls/inspection_policy>
#include <ntl/net/tls/stream>
#include <ntl/handle>
#include <ntl/wfp/connect_redirect>
#include <ntl/wfp/management>

#include "browser_https_inspection_contract.hpp"
#include "browser_log.hpp"
#include "browser_policy.hpp"
#include "browser_policy_diagnostics.hpp"
#include "browser_proxy.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

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

class inspectable_browser_identity_provider final
    : public ntl::net::tls_server_identity_provider {
public:
  explicit inspectable_browser_identity_provider(
      ntl::net::cached_tls_server_identity_provider &inner,
      ntl::net::inspection::ech_frontend_provider &ech,
      ntl::net::inspection::downstream_trust_provider &trust,
      std::span<const std::uint8_t> application_id)
      : inner_(&inner),
        ech_(&ech),
        trust_(&trust) {
    application_id_.reserve(application_id.size());
    for (const std::uint8_t value : application_id)
      application_id_.push_back(
          static_cast<std::byte>(value));
  }

  std::shared_ptr<ntl::net::tls_server_identity>
  select(const ntl::net::tls_client_hello &hello) override {
    ntl::net::inspection::tls_inspection_observation observation;
    observation.transport =
        ntl::net::inspection::encrypted_transport::tcp_tls;
    observation.protocol =
        ntl::net::inspection::application_protocol::http1;
    observation.server_name = hello.server_name();
    observation.protocol_adapter_available = true;
    observation.content_decoder_available = true;
    const auto ech = ech_->inspect(hello);
    if (!ech)
      throw std::system_error(
          static_cast<int>(
              static_cast<NTSTATUS>(ech.status())),
          std::system_category(),
          "ECH frontend");
    const ntl::status applied =
        ntl::net::inspection::apply_ech_result(
            observation, *ech);
    if (!applied.is_ok())
      throw std::system_error(
          static_cast<int>(
              static_cast<NTSTATUS>(applied)),
          std::system_category(),
          "ECH frontend result");
    if (ech->state ==
        ntl::net::inspection::ech_offer_state::decrypted)
      throw std::system_error(
          ERROR_NOT_SUPPORTED,
          std::system_category(),
          "decrypted ECH requires a frontend-owned TLS stream; "
          "Schannel cannot resume from an inner ClientHello");
    const auto trust = trust_->classify(
        {.application_id = application_id_,
         .server_name = observation.server_name});
    observation.downstream_trust_known =
        trust != ntl::net::inspection::
                     downstream_trust_state::unknown;
    observation.downstream_rejected_issued_certificate =
        trust ==
        ntl::net::inspection::downstream_trust_state::pinned;
    const auto decision = policy_.decide(observation);
    if (decision.action !=
        ntl::net::inspection::tls_inspection_action::inspect)
      throw std::system_error(
          ERROR_ACCESS_DISABLED_BY_POLICY,
          std::system_category(),
          "TLS ClientHello is not inspectable under policy");
    return inner_->select(observation.server_name);
  }

private:
  ntl::net::cached_tls_server_identity_provider *inner_;
  ntl::net::inspection::ech_frontend_provider *ech_;
  ntl::net::inspection::downstream_trust_provider *trust_;
  std::vector<std::byte> application_id_;
  ntl::net::inspection::explicit_tls_inspection_policy policy_;
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
      ntl::net::io_completion_context &context,
      socket_owner inbound_native,
      socket_owner outbound_native,
      ntl::net::tls_server_identity_provider &identities,
      ntl::net::inspection::origin_client_identity_provider
          &origin_identities,
      const ntl::net::inspection::content_decoder_registry &decoders,
      browser_html_logger &logger,
      browser_connection_completion &completion)
      : inbound_(context, inbound_native.release()),
        outbound_(context, outbound_native.release()),
        identities_(&identities),
        origin_identities_(&origin_identities),
        decoders_(&decoders),
        logger_(&logger),
        completion_(&completion) {}

  browser_connection(const browser_connection &) = delete;
  browser_connection &
  operator=(const browser_connection &) = delete;

  void start() {
    if (started_)
      throw std::logic_error(
          "browser connection task started twice");
    started_ = true;
    task_.emplace(run(shared_from_this()));
  }

  void stop() noexcept {
    std::lock_guard lock(socket_lock_);
    cancel_sockets_locked();
  }

  bool finished() const noexcept {
    return finished_.load(std::memory_order_acquire);
  }

private:
  static coroutine_task<unsigned> run(
      std::shared_ptr<browser_connection> self) {
    try {
      const auto result = co_await run_browser_proxy(
          self->inbound_, self->outbound_.native_handle(),
          *self->identities_, self->outbound_,
          *self->origin_identities_, *self->decoders_,
          *self->logger_);
      if (result.html_path) {
        std::osyncstream(std::cout)
            << "NTL WFP browser HTTPS HTML logged: host="
            << narrow_dns_name(result.server_name)
            << ", status=" << result.status
            << ", file=" << result.html_path->string() << '\n';
      }
    } catch (const std::exception &error) {
      if (!browser_inspection_stop.load(
              std::memory_order_acquire)) {
        self->logger_->record_error(error.what());
        std::osyncstream(std::cerr)
            << "Browser HTTPS connection closed: "
            << error.what() << '\n';
      }
    } catch (...) {
      if (!browser_inspection_stop.load(
              std::memory_order_acquire)) {
        self->logger_->record_error(
            "unknown browser HTTPS connection failure");
        std::osyncstream(std::cerr)
            << "Browser HTTPS connection closed: unknown failure\n";
      }
    }
    self->finish();
    co_return 0;
  }

  void cancel_sockets_locked() noexcept {
    (void)inbound_.cancel();
    (void)outbound_.cancel();
    const SOCKET inbound = inbound_.native_handle();
    const SOCKET outbound = outbound_.native_handle();
    if (inbound != INVALID_SOCKET)
      (void)::shutdown(inbound, SD_BOTH);
    if (outbound != INVALID_SOCKET)
      (void)::shutdown(outbound, SD_BOTH);
  }

  void finish() noexcept {
    {
      std::lock_guard lock(socket_lock_);
      cancel_sockets_locked();
      inbound_.close();
      outbound_.close();
    }
    finished_.store(true, std::memory_order_release);
    completion_->notify();
  }

  std::mutex socket_lock_;
  ntl::net::async_socket inbound_;
  ntl::net::async_socket outbound_;
  ntl::net::tls_server_identity_provider *identities_;
  ntl::net::inspection::origin_client_identity_provider
      *origin_identities_;
  const ntl::net::inspection::content_decoder_registry *decoders_;
  browser_html_logger *logger_;
  browser_connection_completion *completion_;
  std::optional<coroutine_task<unsigned>> task_;
  std::atomic<bool> finished_{false};
  bool started_ = false;
};

class browser_connection_registry {
public:
  browser_connection_registry() = default;
  browser_connection_registry(
      const browser_connection_registry &) = delete;
  browser_connection_registry &
  operator=(const browser_connection_registry &) = delete;

  ~browser_connection_registry() {
    stop_all();
    wait_for_all();
  }

  void start(
      ntl::net::io_completion_context &context,
      socket_owner inbound,
      socket_owner outbound,
      ntl::net::tls_server_identity_provider &identities,
      ntl::net::inspection::origin_client_identity_provider
          &origin_identities,
      const ntl::net::inspection::content_decoder_registry &decoders,
      browser_html_logger &logger) {
    auto connection = std::make_shared<browser_connection>(
        context, std::move(inbound), std::move(outbound),
        identities, origin_identities, decoders, logger,
        completion_);
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
      const std::size_t observed = completion_.snapshot();
      reap_finished();
      if (!connections_.empty())
        completion_.wait_for_change(observed);
    }
  }

  std::size_t size() const noexcept {
    return connections_.size();
  }

private:
  browser_connection_completion completion_;
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
  browser_html_logger logger(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate;
  const auto certificate_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate.export_public_certificate(certificate_path);
  ntl::net::windows_tls_certificate_issuer issuer(
      certificate.get(),
      {.key_name_prefix = L"crtsys-ntl-wfp-browser",
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
  ntl::net::inspection::unavailable_ech_frontend
      default_ech;
  ntl::net::inspection::inspectable_downstream_trust
      default_downstream_trust;
  ntl::net::inspection::unavailable_origin_client_identity
      default_origin_identity;
  auto &ech = providers.ech
                  ? *providers.ech
                  : static_cast<
                        ntl::net::inspection::ech_frontend_provider &>(
                        default_ech);
  auto &downstream_trust =
      providers.downstream_trust
          ? *providers.downstream_trust
          : static_cast<
                ntl::net::inspection::downstream_trust_provider &>(
                default_downstream_trust);
  auto &origin_identities =
      providers.origin_client_identity
          ? *providers.origin_client_identity
          : static_cast<
                ntl::net::inspection::
                    origin_client_identity_provider &>(
                default_origin_identity);
  ntl::net::cached_tls_server_identity_provider
      identities(issuer, 256);
  inspectable_browser_identity_provider
      inspectable_identities(
          identities, ech, downstream_trust,
          browser_id.bytes());
  ntl::net::inspection::content_decoder_registry content_decoders;
  ntl::net::inspection::register_standard_content_decoders(
       content_decoders);
  auto proxy_listener_v4 = make_listener();
  auto proxy_listener_v6 = make_ipv6_listener();
  std::optional<ntl::wfp::dynamic_session> policy;
  policy.emplace(
      L"crtsys ntl::wfp browser HTTPS inspection");
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
  logger.record_lifecycle(
      "wfp-policy " + policy_diagnostic_message);
  std::cout << "NTL WFP policy diagnostics: "
            << policy_diagnostic_message << '\n';

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
      logger.record_error(
          "cannot query browser inspection stop file");
      break;
    }
    if (stop_file)
      break;

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
      auto handoff =
          ntl::wfp::redirected_connection::capture(inbound.get());
      auto outbound =
          socket_owner(handoff.connect_original());
      connections.start(
          connection_io, std::move(inbound), std::move(outbound),
          inspectable_identities, origin_identities,
          content_decoders, logger);
    } catch (const std::exception &error) {
      logger.record_error(error.what());
      std::osyncstream(std::cerr)
          << "Browser HTTPS accept failed: "
          << error.what() << '\n';
    }
  }

  browser_inspection_stop.store(true, std::memory_order_release);
  logger.record_lifecycle(
      "shutdown-begin connections=" +
      std::to_string(connections.size()));
  proxy_listener_v4.socket.reset();
  proxy_listener_v6.socket.reset();
  connections.stop_all();
  logger.record_lifecycle("connection-tasks-cancelled");
  connections.wait_for_all();
  connection_io.wait_for_idle();
  logger.record_lifecycle("connection-tasks-drained");

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
          browser_id.bytes().size());
  logger.record_lifecycle(telemetry_message);
  std::cout << "NTL WFP QUIC telemetry: "
            << telemetry_message << '\n';
  policy.reset();
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  std::cout
      << "NTL WFP browser HTTPS inspection stopped: html-files="
      << logger.html_files()
      << ", http3-delivered=0"
      << ", policy=removed, application-trust-store-writes=none\n";
  return 0;
}

} // namespace crtsys::wfp_sample::browser_https
