#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <filesystem>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

class capture_log {
public:
  explicit capture_log(std::filesystem::path root);

  void write(
      const wfp_kernel_browser_https_inspection::inspection_record &record);
  void write_summary(
      const wfp_kernel_browser_https_inspection::service_info &before,
      const wfp_kernel_browser_https_inspection::service_info &after,
      std::uint64_t records, std::uint64_t dropped) const;

  const std::filesystem::path &root() const noexcept { return root_; }

private:
  std::filesystem::path root_;
};

} // namespace crtsys::wfp_kernel_browser_https
