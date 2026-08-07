#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/tls/stream>
#include <ntl/net/user/redirected_tls_inspection>
#include <ntl/net/user/task>

#include "browser_log.hpp"

namespace crtsys::wfp_sample::browser_https {

struct browser_proxy_result {
  std::wstring server_name;
  unsigned status = 0;
  std::optional<std::filesystem::path> html_path;
};

std::shared_ptr<ntl::net::user::standard_redirected_tls_inspection>
make_browser_http_dispatcher(
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    std::shared_ptr<browser_html_logger> logger);

} // namespace crtsys::wfp_sample::browser_https
