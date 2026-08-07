#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include "windows_support.hpp"

namespace crtsys::wfp_sample {

class installed_machine_certificate {
public:
  static constexpr std::size_t sha1_size = 20;

  installed_machine_certificate(PCCERT_CONTEXT certificate,
                                std::wstring_view store_name) {
    if (!certificate || store_name.empty())
      throw std::invalid_argument("machine certificate input is empty");
    const std::wstring name(store_name);
    store_ = ::CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG |
            CERT_STORE_MAXIMUM_ALLOWED_FLAG,
        name.c_str());
    if (!store_)
      throw_windows("CertOpenStore(LocalMachine)");
    if (!::CertAddCertificateContextToStore(
            store_, certificate, CERT_STORE_ADD_REPLACE_EXISTING, &stored_))
      throw_windows("CertAddCertificateContextToStore(LocalMachine)");
    DWORD size = static_cast<DWORD>(thumbprint_.size());
    if (!::CertGetCertificateContextProperty(
            stored_, CERT_SHA1_HASH_PROP_ID, thumbprint_.data(), &size) ||
        size != thumbprint_.size())
      throw_windows("CertGetCertificateContextProperty(SHA1)");
  }

  installed_machine_certificate(const installed_machine_certificate &) =
      delete;
  installed_machine_certificate &
  operator=(const installed_machine_certificate &) = delete;

  ~installed_machine_certificate() {
    if (stored_)
      (void)::CertDeleteCertificateFromStore(stored_);
    if (store_)
      (void)::CertCloseStore(store_, 0);
  }

  const std::array<std::byte, sha1_size> &thumbprint() const noexcept {
    return thumbprint_;
  }

private:
  HCERTSTORE store_ = nullptr;
  PCCERT_CONTEXT stored_ = nullptr;
  std::array<std::byte, sha1_size> thumbprint_{};
};

} // namespace crtsys::wfp_sample
