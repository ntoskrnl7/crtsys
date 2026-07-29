#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_proxy.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/net/tls/inspection_policy>

#include "http1_inspection.hpp"
#include "http2_inspection.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

bool client_offered_protocol(
    const ntl::net::tls_client_hello &hello,
    std::string_view protocol) {
  return std::find(
             hello.application_protocols().begin(),
             hello.application_protocols().end(),
             protocol) != hello.application_protocols().end();
}

std::vector<std::string> origin_application_protocols(
    const ntl::net::tls_client_hello &hello) {
  std::vector<std::string> protocols;
  if (client_offered_protocol(hello, "h2"))
    protocols.emplace_back("h2");
  if (client_offered_protocol(hello, "http/1.1"))
    protocols.emplace_back("http/1.1");
  if (protocols.empty())
    protocols.emplace_back("http/1.1");
  return protocols;
}

ntl::net::tls_credentials make_origin_credentials(
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities,
    std::wstring_view server_name) {
  auto selected_identity = origin_identities.select(
      {.server_name = server_name, .acceptable_issuers = {}});
  if (!selected_identity)
    throw std::system_error(
        static_cast<int>(
            static_cast<NTSTATUS>(selected_identity.status())),
        std::system_category(),
        "origin mTLS identity selection");
  return ntl::net::tls_credentials::client(
      {.certificate = *selected_identity
                          ? selected_identity->get()
                          : nullptr});
}

std::shared_ptr<ntl::net::tls_server_identity>
select_browser_identity(
    ntl::net::tls_server_identity_provider &identities,
    const ntl::net::tls_client_hello &hello) {
  auto identity = identities.select(hello);
  if (!identity)
    throw std::runtime_error(
        "TLS identity provider declined browser ClientHello");
  return identity;
}

nested_task<ntl::net::inspection::application_protocol>
negotiate_origin_tls(
    ntl::net::tls_stream &outbound,
    const ntl::net::tls_client_hello &hello,
    std::wstring_view server_name) {
  co_await outbound.handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = nullptr,
       .application_protocols =
           origin_application_protocols(hello),
       .require_application_protocol = false});

  const auto selected =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::tcp_tls,
          outbound.negotiated_application_protocol(), true);
  if (selected.protocol !=
          ntl::net::inspection::application_protocol::http1 &&
      selected.protocol !=
          ntl::net::inspection::application_protocol::http2)
    throw std::runtime_error(
        "origin negotiated an unsupported TLS application protocol");
  if (selected.protocol ==
          ntl::net::inspection::application_protocol::http2 &&
      !client_offered_protocol(hello, "h2"))
    throw std::runtime_error(
        "origin selected h2 that the browser did not offer");
  if (selected.protocol ==
          ntl::net::inspection::application_protocol::http1 &&
      !client_offered_protocol(hello, "http/1.1") &&
      !hello.application_protocols().empty())
    throw std::runtime_error(
        "origin selected HTTP/1.1 that the browser did not offer");
  co_return selected.protocol;
}

nested_task<ntl::net::inspection::application_protocol>
negotiate_browser_tls(
    ntl::net::tls_stream &inbound,
    const ntl::net::tls_client_hello &hello,
    ntl::net::inspection::application_protocol protocol) {
  const bool http2 =
      protocol ==
      ntl::net::inspection::application_protocol::http2;
  co_await inbound.handshake_server(
      hello.initial_ciphertext(),
      {.application_protocols =
           http2 ? std::vector<std::string>{"h2"}
                 : std::vector<std::string>{"http/1.1"},
       .require_application_protocol = http2});
  const auto selected =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::tcp_tls,
          inbound.negotiated_application_protocol(), true);
  co_return selected.protocol;
}

} // namespace

coroutine_task<browser_proxy_result> run_browser_proxy(
    ntl::net::async_socket &inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_server_identity_provider &identities,
    ntl::net::async_socket &outbound_socket_owner,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities,
    const ntl::net::inspection::content_decoder_registry &decoders,
    browser_html_logger &logger) {
  auto hello = co_await ntl::net::read_tls_client_hello(
      inbound_socket,
      {.maximum_buffered_ciphertext = 256 * 1024,
       .maximum_client_hello = 128 * 1024,
       .receive_chunk_size = 4096,
       .maximum_alpn_protocols = 32});
  const std::wstring server_name(hello.server_name());
  if (server_name.empty())
    throw std::runtime_error(
        "browser TLS ClientHello did not contain SNI");

  auto outbound_credentials = make_origin_credentials(
      origin_identities, server_name);
  ntl::net::tls_stream outbound(
      outbound_socket_owner, outbound_credentials);
  const auto outbound_protocol = co_await negotiate_origin_tls(
      outbound, hello, server_name);

  auto identity = select_browser_identity(identities, hello);
  ntl::net::tls_stream inbound(
      inbound_socket, identity->credentials(),
      {.maximum_buffered_ciphertext = 1024 * 1024,
       .receive_chunk_size = 16 * 1024});
  const auto inbound_protocol = co_await negotiate_browser_tls(
      inbound, hello, outbound_protocol);
  if (outbound_protocol != inbound_protocol)
    throw std::runtime_error(
        "browser and origin negotiated different ALPN protocols");
  logger.record_protocol(
      server_name, inbound.negotiated_application_protocol());

  if (outbound_protocol ==
      ntl::net::inspection::application_protocol::http2) {
    co_return co_await relay_http2_connection(
        inbound_socket.native_handle(), outbound_socket,
        inbound, outbound, server_name, decoders, logger);
  }

  co_return co_await relay_http1_connection(
      inbound_socket.native_handle(), outbound_socket,
      inbound, outbound, server_name, decoders, logger);
}

} // namespace crtsys::wfp_sample::browser_https
