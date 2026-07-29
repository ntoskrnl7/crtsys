#pragma once

#include <filesystem>

namespace ntl::net::inspection {
class ech_frontend_provider;
class downstream_trust_provider;
class origin_client_identity_provider;
}

namespace crtsys::wfp_sample::browser_https {

struct browser_inspection_security_providers {
  ntl::net::inspection::ech_frontend_provider *ech = nullptr;
  ntl::net::inspection::downstream_trust_provider
      *downstream_trust = nullptr;
  ntl::net::inspection::origin_client_identity_provider
      *origin_client_identity = nullptr;
};

int run_browser_inspection(
    const std::filesystem::path &browser,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds,
    browser_inspection_security_providers providers = {});

} // namespace crtsys::wfp_sample::browser_https
