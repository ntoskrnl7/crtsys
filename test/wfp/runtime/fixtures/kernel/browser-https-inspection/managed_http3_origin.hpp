#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

/** A deterministic, peer-validating HTTP/3 origin used by managed acceptance. */
class managed_http3_origin {
public:
  managed_http3_origin(
      const std::array<
          std::byte,
          wfp_kernel_browser_https_inspection::certificate_thumbprint_size>
          &certificate_thumbprint,
      std::string server_name = "localhost");
  managed_http3_origin(const managed_http3_origin &) = delete;
  managed_http3_origin &operator=(const managed_http3_origin &) = delete;
  ~managed_http3_origin();

  std::uint16_t port() const noexcept;
  std::uint64_t accepted() const noexcept;
  std::uint64_t requests() const noexcept;
  std::uint64_t transformed_requests() const noexcept;
  std::uint64_t decoder_acknowledgements() const noexcept;
  std::uint64_t qpack_encoder_streams() const noexcept;
  std::uint64_t active_connections() const noexcept;

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace crtsys::wfp_kernel_browser_https
