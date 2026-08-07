#pragma once

#include <filesystem>
#include <memory>

namespace ntl::net::inspection {
class ech_frontend_provider;
class downstream_trust_provider;
class origin_client_identity_provider;
}

namespace crtsys::wfp_sample::browser_https {

struct browser_inspection_security_providers {
  std::shared_ptr<ntl::net::inspection::ech_frontend_provider> ech;
  std::shared_ptr<ntl::net::inspection::downstream_trust_provider>
      downstream_trust;
  std::shared_ptr<ntl::net::inspection::origin_client_identity_provider>
      origin_client_identity;
};

int run_browser_inspection(
    const std::filesystem::path &browser,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds,
    browser_inspection_security_providers providers = {});

} // namespace crtsys::wfp_sample::browser_https
