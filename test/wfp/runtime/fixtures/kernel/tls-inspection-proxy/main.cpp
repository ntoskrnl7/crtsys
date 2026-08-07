#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <ntl/net/tls/certificate>

#include "tls_endpoint_fixture.hpp"
#include "tls_runtime_fixture.hpp"
#include "windows_support.hpp"

namespace {

namespace endpoint = crtsys::test::wfp::tls_endpoint_fixture;
namespace fixture = crtsys::test::wfp::runtime_fixture;
namespace tls_fixture = crtsys::test::wfp::tls_fixture;
using namespace crtsys::wfp_sample;

constexpr std::wstring_view server_name = L"kernel.example";
constexpr std::string_view transform_marker =
    "<!-- inspected by ntl -->";

std::filesystem::path parse_controller(int argc, wchar_t **argv) {
  if (argc == 1)
    return fixture::sibling_executable(
        L"crtsys_wfp_kernel_tls_inspection_proxy_controller.exe");
  if (argc == 3 && std::wstring_view(argv[1]) == L"--controller")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument("usage: acceptance [--controller <path>]");
}

void require_allowed(const endpoint::client_result &client,
                     const endpoint::origin_result &origin) {
  if (!client.received || client.status != 200 ||
      client.body.find("kernel TLS fixture origin accepted") ==
          std::string::npos ||
      client.body.find(transform_marker) == std::string::npos ||
      !origin.received || !origin.transformed_header)
    throw std::runtime_error("kernel TLS permit/transform proof failed");
}

void require_blocked(const endpoint::client_result &client,
                     const listener &origin) {
  if (!client.received || client.status != 403 ||
      endpoint::has_pending_connection(origin))
    throw std::runtime_error("kernel TLS block reached the origin");
}

void send_malformed(int family, std::uint16_t port) {
  auto socket = endpoint::connect_loopback(family, port);
  constexpr char invalid[] = "not tls";
  if (::send(socket.get(), invalid, sizeof(invalid) - 1, 0) !=
      sizeof(invalid) - 1)
    throw_socket("send(kernel malformed TLS)");
  (void)::shutdown(socket.get(), SD_BOTH);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::string_view phase = "initialization";
  std::unique_ptr<fixture::state_directory> state;
  std::unique_ptr<tls_fixture::controller_process> process;
  try {
    winsock_session winsock;
    const auto controller = parse_controller(argc, argv);
    auto origin_v4 = make_listener();
    auto origin_v6 = make_ipv6_listener();
    state = std::make_unique<fixture::state_directory>(
        L"wfp-kernel-tls-inspection");
    const std::vector<tls_fixture::controller_process::option>
        controller_options{
            {L"--ipv4-port", std::to_wstring(origin_v4.port)},
            {L"--ipv6-port", std::to_wstring(origin_v6.port)}};
    process = std::make_unique<tls_fixture::controller_process>(
        controller, state->path(), controller_options, 90'000);
    process->wait_ready();

    auto authority_certificate =
        tls_fixture::load_der_certificate(process->ca_file());
    const auto identity_thumbprint = tls_fixture::load_certificate_thumbprint(
        process->identity_thumbprint_file());
    tls_fixture::machine_certificate origin_certificate(identity_thumbprint);
    auto origin_credentials =
        ntl::net::tls_credentials::server(origin_certificate.get());
    auto client_credentials = ntl::net::tls_credentials::client(
        {.manual_peer_validation = true});
    auto authority =
        std::make_shared<ntl::net::certificate_authority_policy>(
            authority_certificate.get());
    const endpoint::endpoint_options options{
        std::wstring(server_name), "kernel.example",
        "x-ntl-inspected: 1",
        "<html><body>kernel TLS fixture origin accepted</body></html>"};

    phase = "HTTP/1.1 permit";
    auto h1_origin = endpoint::start_origin(
        origin_v4, origin_credentials, options, false);
    const auto h1_allowed = endpoint::exchange_http1(
        AF_INET, origin_v4.port, client_credentials, authority, options,
        false);
    require_allowed(h1_allowed, h1_origin.get());

    phase = "HTTP/1.1 block";
    const auto h1_blocked = endpoint::exchange_http1(
        AF_INET6, origin_v6.port, client_credentials, authority, options,
        true);
    require_blocked(h1_blocked, origin_v6);

    phase = "HTTP/2 permit";
    auto h2_origin = endpoint::start_origin(
        origin_v6, origin_credentials, options, true);
    const auto h2_allowed = endpoint::exchange_http2(
        AF_INET6, origin_v6.port, client_credentials, authority, options,
        false);
    require_allowed(h2_allowed, h2_origin.get());

    phase = "HTTP/2 block";
    const auto h2_blocked = endpoint::exchange_http2(
        AF_INET, origin_v4.port, client_credentials, authority, options,
        true);
    require_blocked(h2_blocked, origin_v4);

    phase = "malformed and idle TLS";
    send_malformed(AF_INET, origin_v4.port);
    {
      auto idle = endpoint::connect_loopback(AF_INET6, origin_v6.port);
      std::this_thread::sleep_for(std::chrono::seconds(4));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (endpoint::has_pending_connection(origin_v4) ||
        endpoint::has_pending_connection(origin_v6))
      throw std::runtime_error("malformed/idle TLS reached an origin");

    phase = "policy removal and direct-connect restoration";
    process->request_policy_removal();
    process->wait_policy_removed();
    endpoint::prove_raw_direct(origin_v4, AF_INET);
    endpoint::prove_raw_direct(origin_v6, AF_INET6);
    process->request_stop();
    process->wait();

    phase = "evidence validation";
    const auto stats = fixture::read_stats(process->stats_file());
    const auto value = [&](std::string_view name) {
      return fixture::require_stat(stats, name);
    };
    if (value("delta.accepted") < 6 ||
        value("delta.handshaken") < 4 ||
        value("delta.permitted") < 2 || value("delta.blocked") < 2 ||
        value("delta.origin_connected") < 2 ||
        value("delta.origin_completed") < 2 ||
        value("delta.failed") < 2 ||
        value("counter_regressions") != 0 ||
        value("capture.permitted") != 2 ||
        value("capture.blocked") != 2 || value("capture.failed") < 2 ||
        value("capture.ipv4") == 0 || value("capture.ipv6") == 0 ||
        value("capture.http1") == 0 || value("capture.http2") == 0 ||
        value("capture.request_transformed") != 4 ||
        value("capture.response_transformed") != 4 ||
        value("capture.wrong_port") != 0 ||
        value("capture.wrong_sni") != 0 ||
        value("capture.invalid_failure_status") != 0 ||
        value("capture.dropped") != 0 ||
        value("credentials_ready") != 1 || value("identity_count") != 1 ||
        value("policy_removed") != 1)
      throw std::runtime_error("kernel TLS controller evidence is incomplete");

    std::wcout
        << L"Kernel TLS inspection acceptance PASS: IPv4/IPv6 original destination, "
           L"redirect records, two-leg system-validated Schannel, "
           L"ALPN HTTP/1.1+HTTP/2, sni=identity-selected, "
           L"common request/response transforms, h1_permit=1, h1_block=1, "
           L"h1_request_transformed=1, h1_response_transformed=1, "
           L"h2_permit=1, h2_block=1, h2_request_transformed=1, "
           L"h2_response_transformed=1, fail-closed block, "
           L"malformed/timeout=2, bounded capture, "
           L"restored=IPv4/IPv6, cleanup.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel TLS inspection acceptance failed during " << phase
              << ": " << error.what() << '\n';
    if (process) {
      try {
        process->request_policy_removal();
        process->wait_policy_removed();
        process->request_stop();
        process->wait();
        if (std::filesystem::is_regular_file(process->stats_file())) {
          std::ifstream input(process->stats_file(), std::ios::binary);
          std::cerr << input.rdbuf();
        }
      } catch (const std::exception &cleanup_error) {
        std::cerr << "Kernel TLS inspection diagnostic cleanup failed: "
                  << cleanup_error.what() << '\n';
      }
    }
    return 1;
  }
}
