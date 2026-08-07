#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <winioctl.h>

#include <array>
#include <cstddef>
#include <string_view>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

class installed_certificate {
public:
  installed_certificate(PCCERT_CONTEXT certificate,
                        std::wstring_view store_name);
  installed_certificate(const installed_certificate &) = delete;
  installed_certificate &operator=(const installed_certificate &) = delete;
  ~installed_certificate();

  const std::array<std::byte,
                   wfp_kernel_browser_https_inspection::
                       certificate_thumbprint_size> &
  thumbprint() const noexcept {
    return thumbprint_;
  }

private:
  HCERTSTORE store_ = nullptr;
  PCCERT_CONTEXT stored_ = nullptr;
  std::array<std::byte,
             wfp_kernel_browser_https_inspection::certificate_thumbprint_size>
      thumbprint_{};
};

} // namespace crtsys::wfp_kernel_browser_https
