#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <filesystem>
#include <optional>
#include <string>

#include <ntl/net/io/async_socket>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/tls/acceptor>
#include <ntl/net/tls/inspection_frontend>
#include <ntl/net/tls/stream>

#include "browser_log.hpp"
#include "coroutine_task.hpp"

namespace crtsys::wfp_sample::browser_https {

struct browser_proxy_result {
  std::wstring server_name;
  unsigned status = 0;
  std::optional<std::filesystem::path> html_path;
};

coroutine_task<browser_proxy_result> run_browser_proxy(
    ntl::net::async_socket &inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_server_identity_provider &identities,
    ntl::net::async_socket &outbound_socket_owner,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities,
    const ntl::net::inspection::content_decoder_registry &decoders,
    browser_html_logger &logger);

} // namespace crtsys::wfp_sample::browser_https
