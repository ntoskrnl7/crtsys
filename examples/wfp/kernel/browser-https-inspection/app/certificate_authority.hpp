#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <filesystem>
#include <string>

namespace crtsys::wfp_kernel_browser_https {

class ephemeral_authority {
public:
  ephemeral_authority();
  ephemeral_authority(const ephemeral_authority &) = delete;
  ephemeral_authority &operator=(const ephemeral_authority &) = delete;
  ~ephemeral_authority();

  PCCERT_CONTEXT get() const noexcept { return certificate_; }
  void export_public_certificate(const std::filesystem::path &path) const;

private:
  std::wstring container_name_;
  HCRYPTPROV provider_ = 0;
  HCRYPTKEY key_ = 0;
  PCCERT_CONTEXT certificate_ = nullptr;
};

} // namespace crtsys::wfp_kernel_browser_https
