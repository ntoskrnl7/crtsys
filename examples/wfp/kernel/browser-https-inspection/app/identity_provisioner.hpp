#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ntl/net/tls/certificate>

#include "certificate_store.hpp"

namespace crtsys::wfp_kernel_browser_https {

class identity_provisioner {
public:
  using configure_callback = std::function<void(
      const wfp_kernel_browser_https_inspection::certificate_config &)>;

  identity_provisioner(configure_callback configure,
                       PCCERT_CONTEXT authority,
                       std::filesystem::path audit_directory);

  bool ensure(std::string_view server_name);
  void replace(std::string_view server_name);
  std::size_t size() const noexcept { return names_.size(); }

private:
  struct provisioned_certificate {
    ntl::net::issued_tls_certificate issued;
    std::unique_ptr<installed_certificate> installed;
  };

  static std::wstring widen_dns_name(std::string_view value);
  void provision(std::string_view normalized, std::string_view operation);

  configure_callback configure_;
  ntl::net::windows_tls_certificate_issuer issuer_;
  std::filesystem::path audit_directory_;
  std::unordered_set<std::string> names_;
  std::vector<provisioned_certificate> certificates_;
};

} // namespace crtsys::wfp_kernel_browser_https
