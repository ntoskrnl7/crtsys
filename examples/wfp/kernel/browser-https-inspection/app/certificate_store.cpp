#include "certificate_store.hpp"

#include <stdexcept>
#include <system_error>

namespace crtsys::wfp_kernel_browser_https {

namespace {

[[noreturn]] void throw_windows(const char *operation) {
  const DWORD error = ::GetLastError();
  throw std::system_error(static_cast<int>(error), std::system_category(),
                          operation);
}

} // namespace

installed_certificate::installed_certificate(
    PCCERT_CONTEXT certificate, std::wstring_view store_name) {
  if (!certificate || store_name.empty())
    throw std::invalid_argument("certificate store input is empty");
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

installed_certificate::~installed_certificate() {
  if (stored_)
    (void)::CertDeleteCertificateFromStore(stored_);
  if (store_)
    (void)::CertCloseStore(store_, 0);
}

} // namespace crtsys::wfp_kernel_browser_https
