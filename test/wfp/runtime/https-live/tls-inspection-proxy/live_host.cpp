#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "live_host.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/inspection/core>
#include <ntl/net/io/async_socket>
#include <ntl/net/tls/acceptor>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/net/user/structured_concurrency>
#include <ntl/net/user/task>
#include <ntl/wfp/connect_redirect>
#include <ntl/wfp/management>

#include "http1_support.hpp"
#include "test_certificate.hpp"
#include "tls_inspection_proxy_contract.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_sample::tls_inspection {
namespace {

using namespace crtsys::wfp_sample;
namespace contract = wfp_tls_inspection_proxy;

std::vector<sockaddr_in> resolve_ipv4_candidates(std::wstring_view host,
                                                 std::uint16_t port) {
  const std::wstring owned(host);
  ADDRINFOW hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  PADDRINFOW results = nullptr;
  const std::wstring service = std::to_wstring(port);
  const int status = ::GetAddrInfoW(owned.c_str(), service.c_str(), &hints,
                                    &results);
  if (status != 0)
    throw std::system_error(status, std::system_category(), "GetAddrInfoW");
  struct release_addresses {
    PADDRINFOW value = nullptr;
    ~release_addresses() {
      if (value)
        ::FreeAddrInfoW(value);
    }
  } release{results};
  std::vector<sockaddr_in> candidates;
  for (auto current = results; current && candidates.size() != 16;
       current = current->ai_next) {
    if (current->ai_family != AF_INET ||
        current->ai_addrlen < sizeof(sockaddr_in))
      continue;
    const auto candidate =
        *reinterpret_cast<const sockaddr_in *>(current->ai_addr);
    const bool duplicate = std::ranges::any_of(
        candidates, [&](const sockaddr_in &existing) {
          return existing.sin_addr.s_addr == candidate.sin_addr.s_addr &&
                 existing.sin_port == candidate.sin_port;
        });
    if (!duplicate)
      candidates.push_back(candidate);
  }
  if (candidates.empty())
    throw std::runtime_error("DNS returned no IPv4 HTTPS address");
  return candidates;
}

std::string format_ipv4(const sockaddr_in &address) {
  std::array<char, INET_ADDRSTRLEN> text{};
  if (!::InetNtopA(AF_INET, const_cast<IN_ADDR *>(&address.sin_addr),
                   text.data(), static_cast<DWORD>(text.size())))
    throw_socket("InetNtopA");
  return text.data();
}

socket_owner connect_ipv4(const sockaddr_in &address) {
  socket_owner socket(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr,
                                   0, WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(HTTPS client)");
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    throw_socket("connect(HTTPS host)");
  return socket;
}

std::string narrow_dns_name(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character <= 0 || character > 0x7f)
      throw std::runtime_error("live HTTPS requires an ASCII DNS name");
    result.push_back(static_cast<char>(character));
  }
  return result;
}

std::vector<std::byte> encode_live_http_request(std::string_view host) {
  if (host.empty())
    throw std::invalid_argument("HTTPS host cannot be empty");
  const std::string request =
      "GET / HTTP/1.1\r\nHost: " + std::string(host) +
      "\r\nUser-Agent: ntl-wfp-https-inspection/1.0\r\n"
      "Accept: text/html,*/*;q=0.8\r\nAccept-Encoding: identity\r\n"
      "Connection: close\r\n\r\n";
  std::vector<std::byte> result(request.size());
  std::memcpy(result.data(), request.data(), request.size());
  return result;
}

std::string body_preview(std::span<const std::byte> body,
                         std::size_t maximum = 240) {
  const auto count = (std::min)(body.size(), maximum);
  std::string result(count, ' ');
  for (std::size_t index = 0; index != count; ++index) {
    const auto value = static_cast<unsigned char>(body[index]);
    result[index] = value >= 0x20 && value < 0x7f
                        ? static_cast<char>(value)
                        : ' ';
  }
  return result;
}

void install_live_policy(ntl::wfp::policy_session &session,
                         const ntl::wfp::application_id &application,
                         const sockaddr_in &destination,
                         std::uint16_t proxy_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key,
         L"crtsys NTL WFP live HTTPS inspection provider",
         L"Dynamic provider for one controlled HTTPS client"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key,
         L"crtsys NTL WFP live HTTPS inspection sublayer",
         L"Redirect only the selected app, IPv4 address, and port", 0x7500});
    const auto callout = transaction.add_callout<contract::layer_v4>(
        provider, {contract::callout_key_v4,
                   L"Redirect one controlled HTTPS connection",
                   L"Typed ALE_CONNECT_REDIRECT_V4 callout"});
    ntl::wfp::connect_redirect_filter_builder<contract::layer_v4> filter(
        contract::filter_key_v4,
        L"Redirect one executable and HTTPS destination",
        {::GetCurrentProcessId(), proxy_port,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter.application_equal(application)
        .protocol_equal(IPPROTO_TCP)
        .remote_address_equal(ntl::wfp::ipv4_address::from_host_order(
            ntohl(destination.sin_addr.s_addr)))
        .remote_port_equal(ntohs(destination.sin_port));
    transaction.add_connect_redirect_filter(sublayer, callout, filter);
  });
}

struct live_proxy_result {
  std::wstring server_name;
  std::uint16_t original_port = 0;
  parsed_http_response response;
};

ntl::net::user::task<live_proxy_result> run_live_proxy(
    ntl::net::async_socket inbound_socket,
    std::shared_ptr<ntl::net::tls_server_identity_provider> identities,
    ntl::net::tls_stream &outbound) {
  auto accepted = co_await ntl::net::accept_tls(
      std::move(inbound_socket), std::move(identities),
      {.maximum_buffered_ciphertext = 256 * 1024,
       .maximum_client_hello = 128 * 1024,
       .receive_chunk_size = 13,
       .maximum_alpn_protocols = 32});
  auto &inbound = accepted.borrowed_stream();
  const std::wstring server_name(
      accepted.client_hello_ref().server_name());
  if (server_name.empty())
    throw std::runtime_error("live HTTPS ClientHello did not contain SNI");
  co_await outbound.handshake_client({server_name, nullptr});
  ntl::net::tls_framed_stream requests(
      inbound, make_http_framer(ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 197);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error("live HTTPS client sent no request");
  const ntl::net::inspection::content_view plaintext(request->frame());
  const auto has_host = plaintext.contains(narrow_dns_name(server_name));
  if (!has_host || !*has_host)
    throw std::runtime_error("decrypted request did not contain its host");
  if (co_await outbound.write_all(request->frame()) != request->size())
    throw std::runtime_error("live HTTPS upstream write completed short");
  ntl::net::tls_framed_stream responses(
      outbound,
      make_http_framer(ntl::net::http::http1_message_kind::response, true),
      {maximum_http_message_size}, 4093);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error("live HTTPS server returned no response");
  auto observed = parse_http_response(*response);
  if (co_await inbound.write_all(response->frame()) != response->size())
    throw std::runtime_error("live HTTPS downstream write completed short");
  co_await inbound.shutdown();
  co_await outbound.shutdown();
  co_return live_proxy_result{server_name, 443, std::move(observed)};
}

ntl::net::user::task<parsed_http_response> run_live_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority,
    std::wstring server_name) {
  const std::string host = narrow_dns_name(server_name);
  co_await stream.handshake_client(
      {.server_name = std::move(server_name),
       .certificate_policy = std::move(authority)});
  const auto request = encode_live_http_request(host);
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error("live HTTPS client write completed short");
  ntl::net::tls_framed_stream responses(
      stream,
      make_http_framer(ntl::net::http::http1_message_kind::response, true),
      {maximum_http_message_size}, 3583);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error("inspection proxy returned no response");
  auto parsed = parse_http_response(*response);
  std::array<std::byte, 1> close_probe{};
  if (co_await stream.read_some_borrowed(close_probe) != 0)
    throw std::runtime_error("inspection proxy sent trailing data");
  co_await stream.shutdown();
  co_return parsed;
}

struct live_exchange_result {
  live_proxy_result proxy;
  parsed_http_response client;
};

ntl::net::user::task<live_exchange_result> run_live_exchange_tasks(
    ntl::net::async_socket proxy_inbound,
    std::shared_ptr<ntl::net::tls_server_identity_provider> identities,
    ntl::net::tls_stream &upstream_tls, ntl::net::tls_stream &client_tls,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority,
    std::wstring server_name,
    ntl::net::io_completion_context context) {
  auto [proxy, client] =
      co_await ntl::net::user::when_all_cancel_on_error(
          run_live_proxy(std::move(proxy_inbound), std::move(identities),
                         upstream_tls),
          run_live_client(client_tls, std::move(authority),
                          std::move(server_name)),
          [context = std::move(context)]() mutable noexcept {
            context.close();
          });
  co_return live_exchange_result{std::move(proxy), std::move(client)};
}

live_exchange_result run_live_exchange(
    std::wstring_view server_name, const sockaddr_in &destination,
    const ntl::wfp::application_id &application,
    ntl::net::tls_credentials &controlled_credentials,
    ntl::net::tls_credentials &upstream_credentials,
    std::shared_ptr<ntl::net::tls_server_identity_provider> identities,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority) {
  auto proxy_listener = make_listener();
  auto policy = ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp controlled live HTTPS inspection");
  install_live_policy(policy, application, destination, proxy_listener.port);
  auto client_native = connect_ipv4(destination);
  auto proxy_inbound_native = accept_one(proxy_listener);
  auto handoff =
      ntl::wfp::redirected_connection::capture(proxy_inbound_native.get());
  const auto *original = reinterpret_cast<const sockaddr_in *>(
      &handoff.original_destination_ref());
  if (original->sin_family != AF_INET ||
      original->sin_addr.s_addr != destination.sin_addr.s_addr ||
      original->sin_port != destination.sin_port)
    throw std::runtime_error("WFP did not preserve the HTTPS destination");
  socket_owner upstream_native(handoff.connect_original());
  ntl::net::io_completion_context context;
  ntl::net::async_socket client_socket(context, client_native.release());
  ntl::net::async_socket proxy_inbound(context,
                                       proxy_inbound_native.release());
  ntl::net::async_socket upstream_socket(context,
                                         upstream_native.release());
  ntl::net::tls_stream client_tls(client_socket, controlled_credentials);
  ntl::net::tls_stream upstream_tls(upstream_socket, upstream_credentials);
  auto result = ntl::net::user::sync_wait(run_live_exchange_tasks(
      std::move(proxy_inbound), std::move(identities), upstream_tls,
      client_tls, std::move(authority), std::wstring(server_name),
      context.share()));
  context.wait_for_idle();
  return result;
}

} // namespace

int run_live_host_sample(std::wstring_view host,
                         bool allow_unavailable_revocation) {
  if (host.empty())
    throw std::invalid_argument("live HTTPS inspection requires a DNS host");
  ephemeral_certificate certificate;
  auto issuer = std::make_shared<ntl::net::windows_tls_certificate_issuer>(
      certificate.get(),
      ntl::net::windows_tls_certificate_issuer_options{
          .key_name_prefix = L"crtsys-ntl-wfp-live-host",
          .rsa_bits = 2048,
          .validity_days = 2,
          .machine_keys = true});
  auto identities =
      std::make_shared<ntl::net::cached_tls_server_identity_provider>(issuer,
                                                                      8);
  auto controlled_credentials = ntl::net::tls_credentials::client(
      {.manual_peer_validation = true});
  ntl::net::tls_client_credential_options upstream_options;
  if (allow_unavailable_revocation) {
    upstream_options.revocation_check =
        ntl::net::tls_certificate_revocation_check::chain_excluding_root;
    upstream_options.ignore_missing_revocation_information = true;
    upstream_options.ignore_offline_revocation = true;
  }
  auto upstream_credentials =
      ntl::net::tls_credentials::client(upstream_options);
  auto authority = std::make_shared<ntl::net::certificate_authority_policy>(
      certificate.get());
  const auto application = ntl::wfp::application_id::current_process();
  std::exception_ptr last_error;
  for (const auto &destination : resolve_ipv4_candidates(host, 443)) {
    std::cout << "Inspecting https://" << narrow_dns_name(host) << "/ at "
              << format_ipv4(destination) << ":443\n";
    try {
      const auto observed = run_live_exchange(
          host, destination, application, controlled_credentials,
          upstream_credentials, identities, authority);
      if (observed.proxy.server_name != host ||
          observed.proxy.original_port != 443 ||
          observed.proxy.response.status != observed.client.status ||
          observed.proxy.response.body != observed.client.body)
        throw std::runtime_error("proxy and client observations differ");
      if (observed.proxy.response.status < 200 ||
          observed.proxy.response.status >= 400 ||
          observed.proxy.response.body.empty())
        throw std::runtime_error("host returned no inspectable response");
      std::cout << "  status=" << observed.proxy.response.status
                << ", type=" << observed.proxy.response.content_type
                << ", decoded-body=" << observed.proxy.response.body.size()
                << " bytes\n  plaintext preview: "
                << body_preview(observed.proxy.response.body) << '\n';
      if (identities->size() != 1)
        throw std::runtime_error("SNI identity cache did not reuse its leaf");
      std::cout << "NTL WFP live HTTPS inspection ok: host="
                << narrow_dns_name(host)
                << ", response=plaintext-observed, "
                   "upstream-certificate=system-validated, revocation="
                << (allow_unavailable_revocation
                        ? "check-when-information-available"
                        : "system-default")
                << ", sni=dynamic, application-trust-store-writes=none\n";
      return 0;
    } catch (...) {
      last_error = std::current_exception();
    }
  }
  if (last_error)
    std::rethrow_exception(last_error);
  throw std::runtime_error("live HTTPS inspection had no address candidate");
}

} // namespace crtsys::wfp_sample::tls_inspection
