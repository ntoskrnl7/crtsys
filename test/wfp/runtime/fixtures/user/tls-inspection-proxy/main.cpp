#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <ntl/net/tls/certificate>

#include "tls_endpoint_fixture.hpp"
#include "tls_runtime_fixture.hpp"
#include "windows_support.hpp"

namespace {

namespace endpoint = crtsys::test::wfp::tls_endpoint_fixture;
namespace fixture = crtsys::test::wfp::runtime_fixture;
namespace tls_fixture = crtsys::test::wfp::tls_fixture;
using namespace crtsys::wfp_sample;

constexpr std::wstring_view server_name = L"service.example.test";
constexpr std::string_view transform_marker =
    "<!-- inspected by ntl -->";

std::filesystem::path parse_service(int argc, wchar_t **argv) {
  if (argc == 1)
    return fixture::sibling_executable(
        L"crtsys_wfp_tls_inspection_proxy_service.exe");
  if (argc == 3 && std::wstring_view(argv[1]) == L"--service")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument("usage: acceptance [--service <path>]");
}

void require_allowed(const endpoint::client_result &client,
                     const endpoint::origin_result &origin) {
  if (!client.received || client.status != 200 ||
      client.body.find("user TLS fixture origin accepted") ==
          std::string::npos ||
      client.body.find(transform_marker) == std::string::npos ||
      !origin.received || !origin.transformed_header)
    throw std::runtime_error("user TLS permit/transform proof failed");
}

void require_blocked(const endpoint::client_result &client,
                     const endpoint::origin_result &origin, bool http2) {
  if (!client.received || client.status != 403)
    throw std::runtime_error(
        http2 ? "user TLS HTTP/2 block proof failed"
              : "user TLS HTTP/1 block proof failed");
  if (origin.received)
    throw std::runtime_error("blocked user TLS content reached the origin");
}

void send_malformed(int family, std::uint16_t port) {
  auto socket = endpoint::connect_loopback(family, port);
  constexpr char invalid[] = "not tls";
  if (::send(socket.get(), invalid, sizeof(invalid) - 1, 0) !=
      sizeof(invalid) - 1)
    throw_socket("send(malformed TLS fixture)");
  (void)::shutdown(socket.get(), SD_BOTH);
}

void require_direct(int family, const listener &origin,
                    ntl::net::tls_credentials &client_credentials,
                    ntl::net::tls_credentials &origin_credentials,
                     std::shared_ptr<ntl::net::tls_peer_certificate_policy>
                         authority,
                    const endpoint::endpoint_options &options) {
  auto origin_task = endpoint::start_origin(
      origin, origin_credentials, options, false);
  const auto client = endpoint::exchange_http1(
      family, origin.port, client_credentials, authority, options, false);
  const auto observed = origin_task.get();
  if (!client.received || client.status != 200 ||
      client.body.find("user TLS fixture origin accepted") ==
          std::string::npos ||
      client.body.find(transform_marker) != std::string::npos ||
      !observed.received || observed.transformed_header)
    throw std::runtime_error("direct TLS proof after policy removal failed");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::string stage = "initialization";
  try {
    winsock_session winsock;
    const auto service = parse_service(argc, argv);
    auto origin_v4 = make_listener();
    auto origin_v6 = make_ipv6_listener();
    fixture::state_directory state(L"wfp-user-tls-inspection");
    tls_fixture::controller_process process(
        service, state.path(),
        {{L"--ipv4-port", std::to_wstring(origin_v4.port)},
         {L"--ipv6-port", std::to_wstring(origin_v6.port)},
         {L"--expected-connections", L"10"}},
        90'000);
    process.wait_ready();

    auto authority_certificate =
        tls_fixture::load_der_certificate(process.ca_file());
    const auto identity_thumbprint = tls_fixture::load_certificate_thumbprint(
        process.identity_thumbprint_file());
    tls_fixture::machine_certificate origin_certificate(identity_thumbprint);
    auto origin_credentials =
        ntl::net::tls_credentials::server(origin_certificate.get());
    auto client_credentials = ntl::net::tls_credentials::client(
        {.manual_peer_validation = true});
    auto authority =
        std::make_shared<ntl::net::certificate_authority_policy>(
            authority_certificate.get());
    const endpoint::endpoint_options options{
        std::wstring(server_name), "service.example.test",
        "x-ntl-inspected: 1",
        "<html><body>user TLS fixture origin accepted</body></html>"};

    for (const auto family : {AF_INET, AF_INET6}) {
      stage = family == AF_INET ? "HTTP/1 permit IPv4"
                                : "HTTP/1 permit IPv6";
      const auto &origin = family == AF_INET ? origin_v4 : origin_v6;
      auto task = endpoint::start_origin(
          origin, origin_credentials, options, false);
      const auto client = endpoint::exchange_http1(
          family, origin.port, client_credentials, authority, options, false);
      require_allowed(client, task.get());
    }
    for (const auto family : {AF_INET, AF_INET6}) {
      stage = family == AF_INET ? "HTTP/1 block IPv4"
                                : "HTTP/1 block IPv6";
      const auto &origin = family == AF_INET ? origin_v4 : origin_v6;
      auto task = endpoint::start_origin(
          origin, origin_credentials, options, false);
      const auto client = endpoint::exchange_http1(
          family, origin.port, client_credentials, authority, options, true);
      require_blocked(client, task.get(), false);
    }
    for (const auto family : {AF_INET, AF_INET6}) {
      stage = family == AF_INET ? "HTTP/2 permit IPv4"
                                : "HTTP/2 permit IPv6";
      const auto &origin = family == AF_INET ? origin_v4 : origin_v6;
      auto task = endpoint::start_origin(
          origin, origin_credentials, options, true);
      const auto client = endpoint::exchange_http2(
          family, origin.port, client_credentials, authority, options, false);
      require_allowed(client, task.get());
    }
    for (const auto family : {AF_INET, AF_INET6}) {
      stage = family == AF_INET ? "HTTP/2 block IPv4"
                                : "HTTP/2 block IPv6";
      const auto &origin = family == AF_INET ? origin_v4 : origin_v6;
      auto task = endpoint::start_origin(
          origin, origin_credentials, options, true);
      const auto client = endpoint::exchange_http2(
          family, origin.port, client_credentials, authority, options, true);
      require_blocked(client, task.get(), true);
    }

    stage = "malformed TLS";
    send_malformed(AF_INET, origin_v4.port);
    send_malformed(AF_INET6, origin_v6.port);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (has_pending_connection(origin_v4) || has_pending_connection(origin_v6))
      throw std::runtime_error("malformed TLS reached a fixture origin");

    stage = "policy removal";
    process.request_policy_removal();
    process.wait_policy_removed();
    stage = "direct HTTP/1 IPv4";
    require_direct(AF_INET, origin_v4, client_credentials,
                   origin_credentials, authority, options);
    stage = "direct HTTP/1 IPv6";
    require_direct(AF_INET6, origin_v6, client_credentials,
                   origin_credentials, authority, options);
    process.request_stop();
    process.wait();

    const auto stats = fixture::read_stats(process.stats_file());
    const auto require = [&](std::string_view name,
                             std::uint64_t expected) {
      if (fixture::require_stat(stats, name) != expected)
        throw std::runtime_error("user TLS service statistic mismatch");
    };
    require("accepted", 10);
    require("failed", 2);
    require("ipv4", 4);
    require("ipv6", 4);
    require("h1.permitted", 2);
    require("h1.blocked", 2);
    require("h2.permitted", 2);
    require("h2.blocked", 2);
    require("h1.request_transformed", 2);
    require("h1.response_transformed", 2);
    require("h2.request_transformed", 2);
    require("h2.response_transformed", 2);
    require("identity_handoff", 8);
    require("wrong_original_port", 0);
    require("wrong_sni", 0);
    require("identity_cache", 1);
    require("policy_removed", 1);

    std::wcout
        << L"NTL WFP TLS inspection acceptance PASS: permit=4, block=4, "
           L"ipv4=verified, ipv6=verified, tls_legs=2, http1=bounded, "
           L"h1_request_transformed=2, h1_response_transformed=2, "
           L"h1_origin_blocked=2, http2=bounded, hpack=bounded, "
           L"h2_redirected=4, h2_alpn=4, h2_origin_forwarded=2, "
           L"h2_origin_blocked=2, request_transform=4, "
           L"response_transform=4, malformed=2, sni=dynamic, "
           L"identity_handoff=8, trust-store=unchanged, restored=direct\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "User TLS inspection acceptance failed at " << stage
              << ": " << error.what() << '\n';
    return 1;
  }
}
