#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <string>

#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/http/transform>
#include <ntl/net/tls/stream>

#include "browser_proxy.hpp"
#include "coroutine_task.hpp"

namespace crtsys::wfp_sample::browser_https {

nested_task<browser_proxy_result> relay_http2_connection(
    SOCKET inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound,
    std::wstring server_name,
    const ntl::net::inspection::content_decoder_registry &decoders,
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_pipeline &transforms,
    browser_html_logger &logger);

} // namespace crtsys::wfp_sample::browser_https
