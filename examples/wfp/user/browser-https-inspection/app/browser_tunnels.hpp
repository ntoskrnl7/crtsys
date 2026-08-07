#pragma once

#include <memory>

#include <ntl/net/user/redirected_tls_inspection>

#include "browser_log.hpp"

namespace crtsys::wfp_sample::browser_https {

std::shared_ptr<
    ntl::net::user::redirected_http1_upgrade_handler_factory>
make_browser_http1_upgrade_factory(
    std::shared_ptr<browser_html_logger> logger);

std::shared_ptr<
    ntl::net::user::redirected_http2_tunnel_handler_factory>
make_browser_http2_tunnel_factory(
    std::shared_ptr<browser_html_logger> logger);

} // namespace crtsys::wfp_sample::browser_https
