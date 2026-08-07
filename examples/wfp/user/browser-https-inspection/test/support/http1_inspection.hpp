#pragma once

#include <memory>
#include <string>

#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/tls/stream>
#include <ntl/net/user/redirected_tls_inspection>
#include <ntl/net/user/task>

#include "browser_proxy.hpp"

namespace crtsys::wfp_sample::browser_https {

std::shared_ptr<
    ntl::net::user::redirected_http1_upgrade_handler_factory>
make_browser_http1_upgrade_factory(
    std::shared_ptr<browser_html_logger> logger);

ntl::net::user::task<browser_proxy_result> relay_http1_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders,
    std::shared_ptr<const ntl::net::http::transform_pipeline> transforms,
    std::shared_ptr<browser_html_logger> logger);

ntl::net::user::task<browser_proxy_result> relay_http1_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    std::shared_ptr<browser_html_logger> logger);

} // namespace crtsys::wfp_sample::browser_https
