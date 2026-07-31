#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/io/async_socket>
#include <ntl/net/http/http1_framing>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/acceptor>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/wfp/connect_redirect>
#include <ntl/wfp/management>

#include "coroutine_task.hpp"
#include "http1_support.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

#include "tls_inspection_proxy_contract.hpp"

namespace {

using namespace crtsys::wfp_sample;

constexpr std::wstring_view inspected_server_name =
    L"service.example.test";

socket_owner connect_loopback(
    int family, std::uint16_t port) {
  socket_owner socket(::WSASocketW(
      family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(client)");
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(IPv4 loopback)");
  } else if (family == AF_INET6) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    if (::connect(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(IPv6 loopback)");
  } else {
    throw std::invalid_argument(
        "loopback connection requires AF_INET or AF_INET6");
  }
  return socket;
}

std::vector<sockaddr_in>
resolve_ipv4_candidates(std::wstring_view host,
                        std::uint16_t port) {
  const std::wstring owned(host);
  ADDRINFOW hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  PADDRINFOW results = nullptr;
  const std::wstring service = std::to_wstring(port);
  const int status = ::GetAddrInfoW(
      owned.c_str(), service.c_str(), &hints, &results);
  if (status != 0)
    throw std::system_error(
        status, std::system_category(), "GetAddrInfoW");
  struct address_release {
    PADDRINFOW value;
    ~address_release() {
      if (value)
        ::FreeAddrInfoW(value);
    }
  } release{results};
  constexpr std::size_t maximum_candidates = 16;
  std::vector<sockaddr_in> candidates;
  candidates.reserve(4);
  for (auto current = results;
       current && candidates.size() < maximum_candidates;
       current = current->ai_next) {
    if (current->ai_family != AF_INET ||
        current->ai_addrlen < sizeof(sockaddr_in))
      continue;
    const auto candidate =
        *reinterpret_cast<const sockaddr_in *>(current->ai_addr);
    const bool duplicate = std::ranges::any_of(
        candidates,
        [&](const sockaddr_in &existing) {
          return existing.sin_addr.s_addr ==
                     candidate.sin_addr.s_addr &&
                 existing.sin_port == candidate.sin_port;
        });
    if (!duplicate)
      candidates.push_back(candidate);
  }
  if (candidates.empty())
    throw std::runtime_error(
        "DNS returned no IPv4 address for the HTTPS host");
  return candidates;
}

std::string format_ipv4(const sockaddr_in &address) {
  std::array<char, INET_ADDRSTRLEN> text{};
  if (!::InetNtopA(
          AF_INET, const_cast<IN_ADDR *>(&address.sin_addr),
          text.data(), static_cast<DWORD>(text.size())))
    throw_socket("InetNtopA");
  return text.data();
}

socket_owner connect_ipv4(const sockaddr_in &address) {
  socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(HTTPS client)");
  if (::connect(
          socket.get(),
          reinterpret_cast<const sockaddr *>(&address),
          sizeof(address)) == SOCKET_ERROR)
    throw_socket("connect(HTTPS host)");
  return socket;
}




std::vector<std::byte>
encode_http_request(std::string_view content) {
  if (content.size() > maximum_http_body_size)
    throw std::overflow_error("sample HTTP body is too large");
  std::string header =
      "POST /inspect HTTP/1.1\r\nHost: service.example.test\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Length: " +
      std::to_string(content.size()) +
      "\r\nConnection: close\r\n\r\n";
  std::vector<std::byte> result(header.size() + content.size());
  std::memcpy(result.data(), header.data(), header.size());
  if (!content.empty())
    std::memcpy(
        result.data() + header.size(),
        content.data(), content.size());
  return result;
}

std::vector<std::byte>
encode_live_http_request(std::string_view host) {
  if (host.empty())
    throw std::invalid_argument("HTTPS host cannot be empty");
  const std::string request =
      "GET / HTTP/1.1\r\nHost: " + std::string(host) +
      "\r\nUser-Agent: ntl-wfp-https-inspection/1.0\r\n"
      "Accept: text/html,*/*;q=0.8\r\n"
      "Accept-Encoding: identity\r\n"
      "Connection: close\r\n\r\n";
  std::vector<std::byte> result(request.size());
  std::memcpy(result.data(), request.data(), request.size());
  return result;
}

std::vector<std::byte>
encode_http_response(std::string_view content) {
  if (content.size() > maximum_http_body_size)
    throw std::overflow_error("sample HTTP reply is too large");
  std::string header =
      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
      "Content-Length: " +
      std::to_string(content.size()) +
      "\r\nConnection: close\r\n\r\n";
  std::vector<std::byte> result(header.size() + content.size());
  std::memcpy(result.data(), header.data(), header.size());
  if (!content.empty())
    std::memcpy(
        result.data() + header.size(),
        content.data(), content.size());
  return result;
}

std::span<const std::byte>
http_body(const ntl::net::framed_message &message) {
  const auto bytes = message.frame();
  for (std::size_t index = 3; index < bytes.size(); ++index) {
    if (bytes[index - 3] == std::byte{'\r'} &&
        bytes[index - 2] == std::byte{'\n'} &&
        bytes[index - 1] == std::byte{'\r'} &&
        bytes[index] == std::byte{'\n'})
      return bytes.subspan(index + 1);
  }
  throw std::runtime_error(
      "validated HTTP/1 message had no header terminator");
}

std::string body_preview(
    std::span<const std::byte> body,
    std::size_t maximum = 240) {
  const std::size_t count = (std::min)(body.size(), maximum);
  std::string result(count, ' ');
  for (std::size_t index = 0; index != count; ++index) {
    const auto value =
        static_cast<unsigned char>(body[index]);
    result[index] =
        value >= 0x20 && value < 0x7f
            ? static_cast<char>(value)
            : ' ';
  }
  return result;
}

std::string as_string(std::span<const std::byte> bytes) {
  if (bytes.empty())
    return {};
  return {
      reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}


ntl::net::inspection::verdict inspect_plaintext(
    const ntl::net::framed_message &frame) {
  ntl::net::inspection::context metadata{
      ntl::net::inspection::content_kind::tls_plaintext,
      ntl::net::inspection::direction::outbound,
      1,
      0,
      0,
  };
  const ntl::net::inspection::content_view whole(frame.frame());
  const ntl::net::inspection::content_view content(http_body(frame));
  const ntl::net::inspection::tcp_message_view message(
      metadata, whole, content);
  return ntl::net::inspection::evaluate(
      [](const ntl::net::inspection::tcp_message_view &value) {
        const auto blocked = value.content().contains("BLOCKME");
        if (!blocked)
          return ntl::net::inspection::verdict::drop_flow;
        return *blocked
                   ? ntl::net::inspection::verdict::drop_flow
                   : ntl::net::inspection::verdict::permit;
      },
      message);
}

struct origin_result {
  bool received = false;
  std::string content;
};

coroutine_task<origin_result>
run_origin(ntl::net::tls_stream &stream) {
  co_await stream.handshake_server();
  ntl::net::tls_framed_stream requests(
      stream,
      make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 257);
  auto request = co_await requests.read_frame_or_eof();
  if (!request) {
    co_await stream.shutdown();
    co_return origin_result{};
  }

  origin_result result{
      true, as_string(http_body(*request))};
  const auto reply =
      encode_http_response("echo:" + result.content);
  if (co_await stream.write_all(reply) != reply.size())
    throw std::runtime_error(
        "TLS origin reply completed short");
  co_await stream.shutdown();
  co_return result;
}

struct proxy_result {
  ntl::net::inspection::verdict verdict =
      ntl::net::inspection::verdict::drop_flow;
  std::uint16_t original_port = 0;
  std::wstring server_name;
};

coroutine_task<proxy_result>
run_proxy(ntl::net::async_socket &inbound_socket,
          ntl::net::tls_server_identity_provider &identities,
          ntl::net::tls_stream &outbound,
          ntl::net::tls_peer_certificate_policy &authority,
          std::uint16_t original_port) {
  auto accepted = co_await ntl::net::accept_tls(
      inbound_socket, identities,
      {.maximum_buffered_ciphertext = 256 * 1024,
       .maximum_client_hello = 128 * 1024,
       .receive_chunk_size = 11,
       .maximum_alpn_protocols = 32});
  auto &inbound = accepted.stream();
  const std::wstring server_name(
      accepted.client_hello().server_name());
  co_await outbound.handshake_client(
      {server_name, &authority});

  ntl::net::tls_framed_stream requests(
      inbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 193);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error(
        "TLS client closed before its HTTP request");

  const auto verdict = inspect_plaintext(*request);
  if (verdict != ntl::net::inspection::verdict::permit) {
    co_await inbound.shutdown();
    co_await outbound.shutdown();
    co_return proxy_result{
        verdict, original_port, server_name};
  }

  if (co_await outbound.write_all(request->frame()) !=
      request->size())
    throw std::runtime_error(
        "TLS proxy upstream write completed short");
  ntl::net::tls_framed_stream replies(
      outbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::response),
      {maximum_http_message_size}, 211);
  auto reply = co_await replies.read_frame_or_eof();
  if (!reply)
    throw std::runtime_error(
        "TLS origin closed before its HTTP response");
  if (co_await inbound.write_all(reply->frame()) !=
      reply->size())
    throw std::runtime_error(
        "TLS proxy downstream write completed short");
  co_await inbound.shutdown();
  co_await outbound.shutdown();
  co_return proxy_result{
      verdict, original_port, server_name};
}

struct client_result {
  bool received_reply = false;
  std::string reply;
};

coroutine_task<client_result>
run_client(ntl::net::tls_stream &stream,
           ntl::net::tls_peer_certificate_policy &authority,
           std::wstring_view server_name,
           std::string_view content) {
  co_await stream.handshake_client(
      {std::wstring(server_name), &authority});
  const auto request = encode_http_request(content);
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error(
        "TLS client request completed short");

  ntl::net::tls_framed_stream replies(
      stream,
      make_http_framer(
          ntl::net::http::http1_message_kind::response),
      {maximum_http_message_size}, 173);
  auto reply = co_await replies.read_frame_or_eof();
  client_result result;
  if (reply) {
    result.received_reply = true;
    result.reply = as_string(http_body(*reply));
    std::array<std::byte, 1> close_probe{};
    if (co_await stream.read_some(close_probe) != 0)
      throw std::runtime_error(
          "TLS proxy sent bytes after the sample reply");
  }
  co_await stream.shutdown();
  co_return result;
}

struct exchange_result {
  proxy_result proxy;
  client_result client;
  origin_result origin;
};

exchange_result run_proxied_exchange(
    int family,
    const listener &origin_listener,
    const listener &proxy_listener,
    ntl::net::tls_credentials &client_credentials,
    ntl::net::tls_server_identity &origin_identity,
    ntl::net::tls_server_identity_provider &identities,
    ntl::net::tls_peer_certificate_policy &authority,
    std::string_view content) {
  auto client_native =
      connect_loopback(family, origin_listener.port);
  auto proxy_inbound_native = accept_one(proxy_listener);
  auto handoff =
      ntl::wfp::redirected_connection::capture(
          proxy_inbound_native.get());
  const auto &destination = handoff.original_destination();
  if (destination.ss_family != family)
    throw std::runtime_error(
        "TLS inspection did not preserve the address family");
  std::uint16_t original_port = 0;
  if (family == AF_INET) {
    original_port = ntohs(
        reinterpret_cast<const sockaddr_in *>(&destination)
            ->sin_port);
  } else if (family == AF_INET6) {
    original_port = ntohs(
        reinterpret_cast<const sockaddr_in6 *>(&destination)
            ->sin6_port);
  } else {
    throw std::invalid_argument(
        "proxied exchange requires AF_INET or AF_INET6");
  }
  auto proxy_outbound_native =
      socket_owner(handoff.connect_original());
  auto origin_native = accept_one(origin_listener);

  ntl::net::io_completion_context context;
  ntl::net::async_socket client_socket(
      context, client_native.release());
  ntl::net::async_socket proxy_inbound(
      context, proxy_inbound_native.release());
  ntl::net::async_socket proxy_outbound(
      context, proxy_outbound_native.release());
  ntl::net::async_socket origin_socket(
      context, origin_native.release());
  ntl::net::tls_stream client_tls(
      client_socket, client_credentials);
  ntl::net::tls_stream proxy_outbound_tls(
      proxy_outbound, client_credentials);
  ntl::net::tls_stream origin_tls(
      origin_socket, origin_identity.credentials());

  auto origin_task = run_origin(origin_tls);
  auto proxy_task = run_proxy(
      proxy_inbound, identities, proxy_outbound_tls, authority,
      original_port);
  auto client_task = run_client(
      client_tls, authority, inspected_server_name, content);

  exchange_result result;
  result.proxy = proxy_task.get();
  result.client = client_task.get();
  result.origin = origin_task.get();
  context.wait_for_idle();
  return result;
}

struct direct_result {
  client_result client;
  origin_result origin;
};

direct_result run_direct_exchange(
    int family,
    const listener &origin_listener,
    ntl::net::tls_credentials &client_credentials,
    ntl::net::tls_server_identity &origin_identity,
    ntl::net::tls_peer_certificate_policy &authority,
    std::string_view content) {
  auto client_native =
      connect_loopback(family, origin_listener.port);
  auto origin_native = accept_one(origin_listener);
  ntl::net::io_completion_context context;
  ntl::net::async_socket client_socket(
      context, client_native.release());
  ntl::net::async_socket origin_socket(
      context, origin_native.release());
  ntl::net::tls_stream client_tls(
      client_socket, client_credentials);
  ntl::net::tls_stream origin_tls(
      origin_socket, origin_identity.credentials());
  auto origin_task = run_origin(origin_tls);
  auto client_task = run_client(
      client_tls, authority, inspected_server_name, content);

  direct_result result;
  result.origin = origin_task.get();
  result.client = client_task.get();
  context.wait_for_idle();
  return result;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t original_port_v4,
                    std::uint16_t proxy_port_v4,
                    std::uint16_t original_port_v6,
                    std::uint16_t proxy_port_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_tls_inspection_proxy::provider_key,
         L"crtsys NTL WFP TLS inspection provider",
         L"Dynamic provider for a Schannel plaintext proxy"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_tls_inspection_proxy::sublayer_key,
         L"crtsys NTL WFP TLS inspection sublayer",
         L"Redirect selected TLS connects to the local proxy",
         0x7500});
    const auto callout_v4 =
        transaction.add_callout<
            wfp_tls_inspection_proxy::layer_v4>(
            provider,
            {wfp_tls_inspection_proxy::callout_key_v4,
             L"Redirect selected IPv4 TLS connects",
             L"Typed ALE_CONNECT_REDIRECT_V4 callout"});
    const auto callout_v6 =
        transaction.add_callout<
            wfp_tls_inspection_proxy::layer_v6>(
            provider,
            {wfp_tls_inspection_proxy::callout_key_v6,
             L"Redirect selected IPv6 TLS connects",
             L"Typed ALE_CONNECT_REDIRECT_V6 callout"});

    ntl::wfp::connect_redirect_filter_builder<
        wfp_tls_inspection_proxy::layer_v4>
        filter_v4(
            wfp_tls_inspection_proxy::filter_key_v4,
            L"Redirect the selected IPv4 TLS destination port",
            {::GetCurrentProcessId(), proxy_port_v4},
            ntl::wfp::callout_unavailable::permit);
    filter_v4
        .description(
            L"IPv4 proxy PID and port are encoded by the typed builder")
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v4);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v4, filter_v4);

    ntl::wfp::connect_redirect_filter_builder<
        wfp_tls_inspection_proxy::layer_v6>
        filter_v6(
            wfp_tls_inspection_proxy::filter_key_v6,
            L"Redirect the selected IPv6 TLS destination port",
            {::GetCurrentProcessId(), proxy_port_v6},
            ntl::wfp::callout_unavailable::permit);
    filter_v6
        .description(
            L"IPv6 proxy PID and port are encoded by the typed builder")
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v6);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v6, filter_v6);
  });
}

void install_live_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    const sockaddr_in &destination,
    std::uint16_t proxy_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_tls_inspection_proxy::provider_key,
         L"crtsys NTL WFP live HTTPS inspection provider",
         L"Dynamic provider for one controlled HTTPS client"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_tls_inspection_proxy::sublayer_key,
         L"crtsys NTL WFP live HTTPS inspection sublayer",
         L"Redirect only the selected app, IPv4 address, and port",
         0x7500});
    const auto callout =
        transaction.add_callout<
            wfp_tls_inspection_proxy::layer_v4>(
            provider,
            {wfp_tls_inspection_proxy::callout_key_v4,
             L"Redirect one controlled HTTPS connection",
             L"Typed ALE_CONNECT_REDIRECT_V4 callout"});

    ntl::wfp::connect_redirect_filter_builder<
        wfp_tls_inspection_proxy::layer_v4>
        filter(
            wfp_tls_inspection_proxy::filter_key_v4,
            L"Redirect one executable and HTTPS destination",
            {::GetCurrentProcessId(), proxy_port},
            ntl::wfp::callout_unavailable::permit);
    filter.description(
              L"Application ID, remote IPv4, TCP, and 443 are exact")
        .application_equal(application)
        .protocol_equal(IPPROTO_TCP)
        .remote_address_equal(
            ntl::wfp::ipv4_address::from_host_order(
                ntohl(destination.sin_addr.s_addr)))
        .remote_port_equal(ntohs(destination.sin_port));
    transaction.add_connect_redirect_filter(
        sublayer, callout, filter);
  });
}


std::string narrow_dns_name(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character <= 0 || character > 0x7f)
      throw std::runtime_error(
          "live HTTPS sample requires an ASCII DNS name");
    result.push_back(static_cast<char>(character));
  }
  return result;
}


struct live_proxy_result {
  std::wstring server_name;
  std::uint16_t original_port = 0;
  parsed_http_response response;
};

coroutine_task<live_proxy_result> run_live_proxy(
    ntl::net::async_socket &inbound_socket,
    ntl::net::tls_server_identity_provider &identities,
    ntl::net::tls_stream &outbound) {
  auto accepted = co_await ntl::net::accept_tls(
      inbound_socket, identities,
      {.maximum_buffered_ciphertext = 256 * 1024,
       .maximum_client_hello = 128 * 1024,
       .receive_chunk_size = 13,
       .maximum_alpn_protocols = 32});
  auto &inbound = accepted.stream();
  const std::wstring server_name(
      accepted.client_hello().server_name());
  if (server_name.empty())
    throw std::runtime_error(
        "live HTTPS ClientHello did not contain SNI");

  co_await outbound.handshake_client(
      {server_name, nullptr});

  ntl::net::tls_framed_stream requests(
      inbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 197);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error(
        "live HTTPS client closed before its request");
  const ntl::net::inspection::content_view plaintext(
      request->frame());
  const auto has_host =
      plaintext.contains(narrow_dns_name(server_name));
  if (!has_host || !*has_host)
    throw std::runtime_error(
        "decrypted live HTTPS request did not contain its host");
  if (co_await outbound.write_all(request->frame()) !=
      request->size())
    throw std::runtime_error(
        "live HTTPS upstream request completed short");

  ntl::net::tls_framed_stream responses(
      outbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::response, true),
      {maximum_http_message_size}, 4093);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error(
        "live HTTPS server closed before its response");
  auto observed = parse_http_response(*response);
  if (co_await inbound.write_all(response->frame()) !=
      response->size())
    throw std::runtime_error(
        "live HTTPS downstream response completed short");

  co_await inbound.shutdown();
  co_await outbound.shutdown();
  co_return live_proxy_result{
      server_name, 443, std::move(observed)};
}

coroutine_task<parsed_http_response> run_live_client(
    ntl::net::tls_stream &stream,
    ntl::net::tls_peer_certificate_policy &authority,
    std::wstring server_name) {
  const std::string host = narrow_dns_name(server_name);
  co_await stream.handshake_client(
      {std::move(server_name), &authority});
  const auto request = encode_live_http_request(host);
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error(
        "live HTTPS client request completed short");

  ntl::net::tls_framed_stream responses(
      stream,
      make_http_framer(
          ntl::net::http::http1_message_kind::response, true),
      {maximum_http_message_size}, 3583);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error(
        "inspection proxy returned no live HTTPS response");
  auto parsed = parse_http_response(*response);
  std::array<std::byte, 1> close_probe{};
  if (co_await stream.read_some(close_probe) != 0)
    throw std::runtime_error(
        "inspection proxy sent bytes after the live response");
  co_await stream.shutdown();
  co_return parsed;
}

struct live_exchange_result {
  live_proxy_result proxy;
  parsed_http_response client;
};

live_exchange_result run_live_exchange(
    std::wstring_view server_name,
    const sockaddr_in &destination,
    const ntl::wfp::application_id &application,
    ntl::net::tls_credentials &controlled_credentials,
    ntl::net::tls_credentials &upstream_credentials,
    ntl::net::tls_server_identity_provider &identities,
    ntl::net::tls_peer_certificate_policy &authority) {
  auto proxy_listener = make_listener();
  auto policy = ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp controlled live HTTPS inspection");
  install_live_policy(
      policy, application, destination, proxy_listener.port);

  auto client_native = connect_ipv4(destination);
  auto proxy_inbound_native = accept_one(proxy_listener);
  auto handoff = ntl::wfp::redirected_connection::capture(
      proxy_inbound_native.get());
  const auto *original = reinterpret_cast<const sockaddr_in *>(
      &handoff.original_destination());
  if (original->sin_family != AF_INET ||
      original->sin_addr.s_addr != destination.sin_addr.s_addr ||
      original->sin_port != destination.sin_port)
    throw std::runtime_error(
        "WFP did not preserve the selected HTTPS destination");
  auto upstream_native =
      socket_owner(handoff.connect_original());

  ntl::net::io_completion_context context;
  ntl::net::async_socket client_socket(
      context, client_native.release());
  ntl::net::async_socket proxy_inbound(
      context, proxy_inbound_native.release());
  ntl::net::async_socket upstream_socket(
      context, upstream_native.release());
  ntl::net::tls_stream client_tls(
      client_socket, controlled_credentials);
  ntl::net::tls_stream upstream_tls(
      upstream_socket, upstream_credentials);

  auto proxy_task = run_live_proxy(
      proxy_inbound, identities, upstream_tls);
  auto client_task = run_live_client(
      client_tls, authority, std::wstring(server_name));
  live_exchange_result result;
  result.proxy = proxy_task.get();
  result.client = client_task.get();
  context.wait_for_idle();
  return result;
}


int run_live_host_sample(
    std::wstring_view host,
    bool allow_unavailable_revocation) {
  if (host.empty())
    throw std::invalid_argument(
        "live HTTPS inspection requires a DNS host");
  ephemeral_certificate certificate;
  ntl::net::windows_tls_certificate_issuer issuer(
      certificate.get(),
      {.key_name_prefix = L"crtsys-ntl-wfp-live-host",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true});
  ntl::net::cached_tls_server_identity_provider
      identities(issuer, 8);
  auto controlled_credentials =
      ntl::net::tls_credentials::client(
          {.manual_peer_validation = true});
  ntl::net::tls_client_credential_options
      upstream_options;
  if (allow_unavailable_revocation) {
    upstream_options.revocation_check =
        ntl::net::tls_certificate_revocation_check::
            chain_excluding_root;
    upstream_options.ignore_missing_revocation_information =
        true;
    upstream_options.ignore_offline_revocation = true;
  }
  auto upstream_credentials =
      ntl::net::tls_credentials::client(upstream_options);
  ntl::net::certificate_authority_policy authority(
      certificate.get());
  const auto application =
      ntl::wfp::application_id::current_process();

  const auto inspect_host =
      [&](std::wstring_view host) {
        const auto destinations =
            resolve_ipv4_candidates(host, 443);
        std::exception_ptr last_error;
        for (const auto &destination : destinations) {
          std::cout
              << "Inspecting https://" << narrow_dns_name(host)
              << "/ at " << format_ipv4(destination) << ":443\n";
          try {
            auto result = run_live_exchange(
                host, destination, application,
                controlled_credentials, upstream_credentials,
                identities, authority);
            if (result.proxy.server_name != host ||
                result.proxy.original_port != 443 ||
                result.proxy.response.status !=
                    result.client.status ||
                result.proxy.response.body !=
                    result.client.body)
              throw std::runtime_error(
                  "proxy and client live HTTPS observations differ");
            std::cout
                << "  status=" << result.proxy.response.status
                << ", type="
                << result.proxy.response.content_type
                << ", decoded-body="
                << result.proxy.response.body.size()
                << " bytes\n"
                << "  plaintext preview: "
                << body_preview(result.proxy.response.body)
                << '\n';
            return result;
          } catch (const std::exception &error) {
            last_error = std::current_exception();
            std::cerr
                << "  address "
                << format_ipv4(destination)
                << " failed: " << error.what() << '\n';
          }
        }
        if (last_error)
          std::rethrow_exception(last_error);
        throw std::runtime_error(
            "live HTTPS inspection had no address candidate");
      };

  const auto observed = inspect_host(host);
  if (observed.proxy.response.status < 200 ||
      observed.proxy.response.status >= 400 ||
      observed.proxy.response.body.empty())
    throw std::runtime_error(
        "live host did not return an inspectable HTTPS response");
  if (ascii_contains_ci(
          observed.proxy.response.content_type, "text/html")) {
    const std::string body =
        as_string(observed.proxy.response.body);
    if (!(ascii_contains_ci(body, "<html") ||
          ascii_contains_ci(body, "<!doctype html")))
      throw std::runtime_error(
          "live HTML response did not contain an HTML document");
  }
  if (identities.size() != 1)
    throw std::runtime_error(
        "live SNI identity cache did not retain the selected host");

  std::cout
      << "NTL WFP live HTTPS inspection ok: "
      << "host=" << narrow_dns_name(host)
      << ", response=plaintext-observed, "
         "upstream-certificate=system-validated, "
      << "revocation="
      << (allow_unavailable_revocation
              ? "check-when-information-available"
              : "system-default")
      << ", "
         "sni=dynamic, application-trust-store-writes=none\n";
  return 0;
}

void require_permitted_exchange(
    const exchange_result &result,
    std::uint16_t original_port,
    std::string_view content) {
  if (result.proxy.original_port != original_port ||
      result.proxy.server_name != inspected_server_name ||
      result.proxy.verdict !=
          ntl::net::inspection::verdict::permit ||
      !result.origin.received ||
      result.origin.content != content ||
      !result.client.received_reply ||
      result.client.reply !=
          "echo:" + std::string(content))
    throw std::runtime_error(
        "permitted TLS plaintext proof did not match");
}

void require_blocked_exchange(
    const exchange_result &result,
    std::uint16_t original_port) {
  if (result.proxy.original_port != original_port ||
      result.proxy.server_name != inspected_server_name ||
      result.proxy.verdict !=
          ntl::net::inspection::verdict::drop_flow ||
      result.origin.received ||
      result.client.received_reply)
    throw std::runtime_error(
        "blocked TLS plaintext proof did not match");
}

void require_direct_exchange(
    const direct_result &result,
    std::string_view content) {
  if (!result.origin.received ||
      result.origin.content != content ||
      !result.client.received_reply ||
      result.client.reply !=
          "echo:" + std::string(content))
    throw std::runtime_error(
        "direct TLS exchange after policy removal failed");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    std::wcout << std::unitbuf;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    winsock_session winsock;
    if ((argc == 3 || argc == 4) &&
        std::wstring_view(argv[1]) == L"--inspect-host") {
      const bool allow_unavailable_revocation =
          argc == 4 &&
          std::wstring_view(argv[3]) ==
              L"--allow-unavailable-revocation";
      if (argc == 4 && !allow_unavailable_revocation)
        throw std::invalid_argument(
            "unknown live HTTPS inspection option");
      return run_live_host_sample(
          argv[2], allow_unavailable_revocation);
    }
    if (argc != 1)
      throw std::invalid_argument(
          "usage: crtsys_wfp_tls_inspection_proxy_app.exe "
          "[--inspect-host <dns-host> "
          "[--allow-unavailable-revocation]]");

    ephemeral_certificate certificate;
    ntl::net::windows_tls_certificate_issuer origin_issuer(
        certificate.get(),
        {.key_name_prefix = L"crtsys-ntl-wfp-origin",
         .rsa_bits = 2048,
         .validity_days = 2,
         .machine_keys = true});
    ntl::net::windows_tls_certificate_issuer
        interception_issuer(
            certificate.get(),
            {.key_name_prefix = L"crtsys-ntl-wfp-interception",
             .rsa_bits = 2048,
             .validity_days = 2,
             .machine_keys = true});
    ntl::net::cached_tls_server_identity_provider
        origin_identities(origin_issuer, 2);
    ntl::net::cached_tls_server_identity_provider
        interception_identities(interception_issuer, 32);
    auto origin_identity =
        origin_identities.select(inspected_server_name);
    auto client_credentials =
        ntl::net::tls_credentials::client(
            {.manual_peer_validation = true});
    ntl::net::certificate_authority_policy authority(
        certificate.get());
    auto origin_v4 = make_listener();
    auto proxy_v4 = make_listener();
    auto origin_v6 = make_ipv6_listener();
    auto proxy_v6 = make_ipv6_listener();

    std::wcout
        << L"[1/8] IPv4 TLS origin=" << origin_v4.port
        << L", proxy=" << proxy_v4.port
        << L"; IPv6 TLS origin=" << origin_v6.port
        << L", proxy=" << proxy_v6.port << L".\n";

    exchange_result allowed_v4;
    exchange_result allowed_v6;
    exchange_result blocked_v4;
    exchange_result blocked_v6;
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp TLS inspection proxy sample");
      install_policy(
          policy, origin_v4.port, proxy_v4.port,
          origin_v6.port, proxy_v6.port);
      std::wcout
          << L"[2/8] Selected IPv4 and IPv6 connects now terminate TLS in the "
             L"user-mode proxy.\n";

      constexpr std::string_view allowed_content =
          "ALLOW:ntl-tls-plaintext";
      allowed_v4 = run_proxied_exchange(
          AF_INET, origin_v4, proxy_v4, client_credentials,
          *origin_identity, interception_identities, authority,
          allowed_content);
      require_permitted_exchange(
          allowed_v4, origin_v4.port, allowed_content);
      std::wcout
          << L"[3/8] IPv4 decrypted ALLOW content reached the TLS "
             L"origin and its reply returned.\n";

      allowed_v6 = run_proxied_exchange(
          AF_INET6, origin_v6, proxy_v6, client_credentials,
          *origin_identity,
          interception_identities, authority, allowed_content);
      require_permitted_exchange(
          allowed_v6, origin_v6.port, allowed_content);
      std::wcout
          << L"[4/8] IPv6 decrypted ALLOW content reached the TLS "
             L"origin and its reply returned.\n";

      constexpr std::string_view blocked_content =
          "BLOCKME:ntl-tls-plaintext";
      blocked_v4 = run_proxied_exchange(
          AF_INET, origin_v4, proxy_v4, client_credentials,
          *origin_identity, interception_identities, authority,
          blocked_content);
      require_blocked_exchange(blocked_v4, origin_v4.port);
      blocked_v6 = run_proxied_exchange(
          AF_INET6, origin_v6, proxy_v6, client_credentials,
          *origin_identity,
          interception_identities, authority, blocked_content);
      require_blocked_exchange(blocked_v6, origin_v6.port);
      std::wcout
          << L"[5/8] IPv4 and IPv6 decrypted BLOCKME content was "
             L"closed before either origin received a frame.\n";
    }

    constexpr std::string_view restored_content =
        "BLOCKME:policy-removed";
    const auto restored_v4 = run_direct_exchange(
        AF_INET, origin_v4, client_credentials, *origin_identity,
        authority, restored_content);
    require_direct_exchange(restored_v4, restored_content);
    std::wcout
        << L"[6/8] Ephemeral policy removed; IPv4 reached the "
           L"origin directly.\n";
    const auto restored_v6 = run_direct_exchange(
        AF_INET6, origin_v6, client_credentials, *origin_identity,
        authority,
        restored_content);
    require_direct_exchange(restored_v6, restored_content);
    std::wcout
        << L"[7/8] Ephemeral policy removed; IPv6 reached the "
           L"origin directly.\n";

    if (has_pending_connection(proxy_v4) ||
        has_pending_connection(proxy_v6))
      throw std::runtime_error(
          "TLS proxy received a connection after policy removal");
    if (interception_identities.size() != 1)
      throw std::runtime_error(
          "SNI certificate cache did not reuse one host identity");
    std::wcout
        << L"[8/8] Neither proxy received a connection after removal.\n";
    std::wcout
        << L"NTL WFP TLS inspection-proxy ok: permit=2, block=2, "
           L"ipv4=verified, ipv6=verified, "
           L"http1=bounded, sni=dynamic, certificate=per-host, "
           L"cache=bounded, trust-store=unchanged, restored=direct\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr
        << "NTL WFP TLS inspection-proxy failed: "
        << error.what() << '\n';
    return 1;
  }
}
