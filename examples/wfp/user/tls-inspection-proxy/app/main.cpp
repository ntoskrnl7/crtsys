#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/tls/certificate>
#include <ntl/wfp/management>

#include "machine_certificate.hpp"
#include "proxy_engine.hpp"
#include "runtime_controller.hpp"
#include "test_certificate.hpp"
#include "tls_inspection_proxy_contract.hpp"
#include "tls_runtime_control.hpp"
#include "windows_support.hpp"

namespace {

namespace contract = wfp_tls_inspection_proxy;
namespace runtime = crtsys::examples::wfp::runtime;
namespace tls_runtime = crtsys::examples::wfp::tls_runtime;
namespace inspection = crtsys::wfp_sample::tls_inspection;
using namespace crtsys::wfp_sample;

constexpr std::wstring_view inspected_server_name =
    L"service.example.test";

struct service_counters {
  std::atomic<std::uint64_t> accepted{0};
  std::atomic<std::uint64_t> failed{0};
  std::atomic<std::uint64_t> ipv4{0};
  std::atomic<std::uint64_t> ipv6{0};
  std::atomic<std::uint64_t> h1_permitted{0};
  std::atomic<std::uint64_t> h1_blocked{0};
  std::atomic<std::uint64_t> h2_permitted{0};
  std::atomic<std::uint64_t> h2_blocked{0};
  std::atomic<std::uint64_t> h1_request_transformed{0};
  std::atomic<std::uint64_t> h1_response_transformed{0};
  std::atomic<std::uint64_t> h2_request_transformed{0};
  std::atomic<std::uint64_t> h2_response_transformed{0};
  std::atomic<std::uint64_t> identity_handoff{0};
  std::atomic<std::uint64_t> wrong_original_port{0};
  std::atomic<std::uint64_t> wrong_sni{0};
};

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t original_port_v4,
                    std::uint16_t proxy_port_v4,
                    std::uint16_t original_port_v6,
                    std::uint16_t proxy_port_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key,
         L"crtsys NTL WFP TLS inspection provider",
         L"Dynamic provider for a Schannel plaintext proxy"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key,
         L"crtsys NTL WFP TLS inspection sublayer",
         L"Redirect selected TLS connects to the local proxy", 0x7500});
    const auto callout_v4 =
        transaction.add_callout<contract::layer_v4>(
            provider, {contract::callout_key_v4,
                       L"Redirect selected IPv4 TLS connects",
                       L"Typed ALE_CONNECT_REDIRECT_V4 callout"});
    const auto callout_v6 =
        transaction.add_callout<contract::layer_v6>(
            provider, {contract::callout_key_v6,
                       L"Redirect selected IPv6 TLS connects",
                       L"Typed ALE_CONNECT_REDIRECT_V6 callout"});

    ntl::wfp::connect_redirect_filter_builder<contract::layer_v4> filter_v4(
        contract::filter_key_v4,
        L"Redirect the selected IPv4 TLS destination port",
        {::GetCurrentProcessId(), proxy_port_v4,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v4.protocol_equal(IPPROTO_TCP).remote_port_equal(original_port_v4);
    transaction.add_connect_redirect_filter(sublayer, callout_v4, filter_v4);

    ntl::wfp::connect_redirect_filter_builder<contract::layer_v6> filter_v6(
        contract::filter_key_v6,
        L"Redirect the selected IPv6 TLS destination port",
        {::GetCurrentProcessId(), proxy_port_v6,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v6.protocol_equal(IPPROTO_TCP).remote_port_equal(original_port_v6);
    transaction.add_connect_redirect_filter(sublayer, callout_v6, filter_v6);
  });
}

void record_result(service_counters &counters,
                   const inspection::proxy_connection_result &result,
                   std::uint16_t original_v4,
                   std::uint16_t original_v6) {
  if (result.address_family == AF_INET) {
    ++counters.ipv4;
    if (result.original_port != original_v4)
      ++counters.wrong_original_port;
  } else if (result.address_family == AF_INET6) {
    ++counters.ipv6;
    if (result.original_port != original_v6)
      ++counters.wrong_original_port;
  } else {
    ++counters.wrong_original_port;
  }
  if (result.server_name != inspected_server_name)
    ++counters.wrong_sni;
  if (result.process_id != 0 && result.application_id_size != 0)
    ++counters.identity_handoff;

  const bool permitted =
      result.action == inspection::proxy_action::permitted;
  if (result.protocol == inspection::proxy_protocol::http1) {
    ++(permitted ? counters.h1_permitted : counters.h1_blocked);
    if (result.request_transformed)
      ++counters.h1_request_transformed;
    if (result.response_transformed)
      ++counters.h1_response_transformed;
  } else {
    ++(permitted ? counters.h2_permitted : counters.h2_blocked);
    if (result.request_transformed)
      ++counters.h2_request_transformed;
    if (result.response_transformed)
      ++counters.h2_response_transformed;
  }
}

class service_completion final
    : public ntl::net::user::redirected_tls_session_completion_sink {
public:
  service_completion(std::shared_ptr<service_counters> counters,
                      std::shared_ptr<inspection::proxy_policy_runtime> policy,
                      std::uint16_t original_v4,
                      std::uint16_t original_v6)
      : counters_(std::move(counters)), policy_(std::move(policy)),
        original_v4_(original_v4),
        original_v6_(original_v6) {}

  void completed(
      const ntl::net::user::redirected_tls_session_result &session)
      noexcept override {
    const auto observation =
        policy_->take(session.connection.connection_id);
    const auto result = inspection::make_proxy_connection_result(
        session, observation);
    record_result(*counters_, result, original_v4_, original_v6_);
  }

  void failed(const ntl::net::http::inspection_session_metadata &metadata,
              std::exception_ptr failure) noexcept override {
    (void)policy_->take(metadata.connection.connection_id.value_or(0));
    ++counters_->failed;
    try {
      if (failure)
        std::rethrow_exception(failure);
    } catch (const std::exception &error) {
      std::cerr << "TLS proxy rejected a connection: " << error.what()
                << '\n';
    } catch (...) {
      std::cerr << "TLS proxy rejected a connection: unknown failure\n";
    }
  }

private:
  std::shared_ptr<service_counters> counters_;
  std::shared_ptr<inspection::proxy_policy_runtime> policy_;
  std::uint16_t original_v4_ = 0;
  std::uint16_t original_v6_ = 0;
};

SOCKET accept_ready(const listener &ipv4, const listener &ipv6,
                    std::uint32_t duration_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(duration_ms);
  for (;;) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(ipv4.socket.get(), &readable);
    FD_SET(ipv6.socket.get(), &readable);
    timeval timeout{};
    timeout.tv_usec = 100'000;
    const int selected = ::select(0, &readable, nullptr, nullptr, &timeout);
    if (selected == SOCKET_ERROR)
      throw_socket("select(TLS proxy listeners)");
    const SOCKET source =
        FD_ISSET(ipv4.socket.get(), &readable)
            ? ipv4.socket.get()
            : FD_ISSET(ipv6.socket.get(), &readable)
                  ? ipv6.socket.get()
                  : INVALID_SOCKET;
    if (source != INVALID_SOCKET) {
      const SOCKET accepted = ::accept(source, nullptr, nullptr);
      if (accepted == INVALID_SOCKET)
        throw_socket("accept(TLS proxy)");
      return accepted;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("TLS proxy timed out waiting for connections");
  }
}

std::string stats_text(const service_counters &counters,
                       std::uint16_t original_v4,
                       std::uint16_t original_v6,
                       std::size_t identity_count) {
  std::ostringstream output;
  output << "accepted=" << counters.accepted.load() << '\n'
         << "failed=" << counters.failed.load() << '\n'
         << "ipv4=" << counters.ipv4.load() << '\n'
         << "ipv6=" << counters.ipv6.load() << '\n'
         << "h1.permitted=" << counters.h1_permitted.load() << '\n'
         << "h1.blocked=" << counters.h1_blocked.load() << '\n'
         << "h2.permitted=" << counters.h2_permitted.load() << '\n'
         << "h2.blocked=" << counters.h2_blocked.load() << '\n'
         << "h1.request_transformed="
         << counters.h1_request_transformed.load() << '\n'
         << "h1.response_transformed="
         << counters.h1_response_transformed.load() << '\n'
         << "h2.request_transformed="
         << counters.h2_request_transformed.load() << '\n'
         << "h2.response_transformed="
         << counters.h2_response_transformed.load() << '\n'
         << "identity_handoff=" << counters.identity_handoff.load() << '\n'
         << "wrong_original_port=" << counters.wrong_original_port.load()
         << '\n'
         << "wrong_sni=" << counters.wrong_sni.load() << '\n'
         << "policy.original_v4=" << original_v4 << '\n'
         << "policy.original_v6=" << original_v6 << '\n'
         << "identity_cache=" << identity_count << '\n'
         << "policy_removed=1\n";
  return output.str();
}

int run_service(int argc, wchar_t **argv) {
  runtime::arguments arguments(argc, argv);
  const auto original_v4 = arguments.required_port(L"--ipv4-port");
  const auto original_v6 = arguments.required_port(L"--ipv6-port");
  const auto expected_connections = arguments.optional_u32(
      L"--expected-connections", 10, 1, 128);
  const auto lifecycle = tls_runtime::parse_lifecycle(arguments);
  arguments.finish();

  winsock_session winsock;
  ephemeral_certificate authority;
  ntl::net::windows_tls_certificate_issuer origin_issuer(
      authority.get(),
      {.key_name_prefix = L"crtsys-ntl-wfp-origin-service",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true});
  auto interception_issuer =
      std::make_shared<ntl::net::windows_tls_certificate_issuer>(
      authority.get(),
      ntl::net::windows_tls_certificate_issuer_options{
          .key_name_prefix = L"crtsys-ntl-wfp-interception-service",
          .rsa_bits = 2048,
          .validity_days = 2,
          .machine_keys = true});
  auto origin_leaf = origin_issuer.issue(inspected_server_name);
  installed_machine_certificate published_origin(
      origin_leaf.borrowed_certificate(), L"MY");
  const auto &identity_thumbprint = published_origin.thumbprint();
  runtime::write_file(
      lifecycle.identity_thumbprint_file,
      {reinterpret_cast<const char *>(identity_thumbprint.data()),
       identity_thumbprint.size()});
  auto identities =
      std::make_shared<ntl::net::cached_tls_server_identity_provider>(
          interception_issuer, 32);
  auto upstream_authority =
      std::make_shared<ntl::net::certificate_authority_policy>(
          authority.get());
  auto proxy_v4 = make_listener();
  auto proxy_v6 = make_ipv6_listener();
  if (!lifecycle.ca_file.parent_path().empty())
    std::filesystem::create_directories(lifecycle.ca_file.parent_path());
  authority.export_public_certificate(lifecycle.ca_file);

  auto counters_owner = std::make_shared<service_counters>();
  auto &counters = *counters_owner;
  auto inspection_policy =
      std::make_shared<inspection::proxy_policy_runtime>();
  ntl::net::io_completion_context connection_io;
  auto completion = std::make_shared<service_completion>(
      counters_owner, inspection_policy, original_v4, original_v6);
  ntl::net::user::redirected_tls_session_registry connections(
      connection_io, 128, identities, inspection_policy->dispatcher(),
      completion,
      {.peer_certificate_policy = upstream_authority});
  {
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp TLS inspection proxy service");
    install_policy(policy, original_v4, proxy_v4.port, original_v6,
                   proxy_v6.port);
    tls_runtime::signal_ready(lifecycle);
    std::wcout << L"TLS proxy service ready: IPv4=" << proxy_v4.port
               << L", IPv6=" << proxy_v6.port << L".\n";

    for (std::uint32_t index = 0; index != expected_connections; ++index) {
      const SOCKET accepted =
          accept_ready(proxy_v4, proxy_v6, lifecycle.duration_ms);
      ++counters.accepted;
      try {
        const auto started = connections.start(accepted);
        if (started != ntl::net::user::redirected_tls_start_result::started) {
          ++counters.failed;
          std::cerr << "TLS proxy rejected a connection: session capacity "
                       "is unavailable\n";
        }
      } catch (const std::exception &error) {
        ++counters.failed;
        std::cerr << "TLS proxy rejected a connection: " << error.what()
                  << '\n';
      }
    }
    connections.close();
    connection_io.wait_for_idle();
    tls_runtime::wait_for_file(lifecycle.remove_policy_file,
                               lifecycle.duration_ms, [] {});
  }
  tls_runtime::signal_policy_removed(lifecycle);
  tls_runtime::wait_for_file(lifecycle.stop_file, lifecycle.duration_ms,
                             [] {});
  runtime::write_file(
      lifecycle.stats_file,
      stats_text(counters, original_v4, original_v6, identities->size()));
  std::wcout << L"TLS proxy service stopped; ephemeral WFP policy removed.\n";
  return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    std::wcout << std::unitbuf;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    return run_service(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP TLS proxy service failed: " << error.what()
              << '\n';
    return 1;
  }
}
