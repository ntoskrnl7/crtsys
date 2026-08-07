#include "certificate_authority.hpp"

#include <array>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <ntl/net/tls/certificate>

namespace crtsys::wfp_kernel_browser_https {
namespace {

[[noreturn]] void fail(const char *operation) {
  throw std::system_error(::GetLastError(), std::system_category(), operation);
}

} // namespace

ephemeral_authority::ephemeral_authority() {
  container_name_ = L"crtsys-kernel-browser-ca-" +
                    std::to_wstring(::GetCurrentProcessId()) + L"-" +
                    std::to_wstring(::GetTickCount64());
  if (!::CryptAcquireContextW(&provider_, container_name_.c_str(),
                              MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
                              CRYPT_NEWKEYSET | CRYPT_MACHINE_KEYSET |
                                  CRYPT_SILENT))
    fail("CryptAcquireContextW(kernel browser CA)");
  if (!::CryptGenKey(provider_, AT_KEYEXCHANGE,
                     (2048u << 16) | CRYPT_EXPORTABLE, &key_))
    fail("CryptGenKey(kernel browser CA)");

  constexpr wchar_t subject[] =
      L"CN=crtsys NTL kernel browser HTTPS inspection CA";
  DWORD encoded_size = 0;
  if (!::CertStrToNameW(X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
                        nullptr, nullptr, &encoded_size, nullptr))
    fail("CertStrToNameW(kernel browser CA size)");
  std::vector<BYTE> encoded(encoded_size);
  if (!::CertStrToNameW(X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
                        nullptr, encoded.data(), &encoded_size, nullptr))
    fail("CertStrToNameW(kernel browser CA)");

  CERT_NAME_BLOB name{encoded_size, encoded.data()};
  CRYPT_ALGORITHM_IDENTIFIER signature{};
  signature.pszObjId = const_cast<char *>(szOID_RSA_SHA256RSA);
  SYSTEMTIME start{};
  ::GetSystemTime(&start);
  SYSTEMTIME end = start;
  ++end.wYear;
  CRYPT_KEY_PROV_INFO key_info{};
  key_info.pwszContainerName = container_name_.data();
  key_info.pwszProvName = const_cast<wchar_t *>(MS_ENH_RSA_AES_PROV_W);
  key_info.dwProvType = PROV_RSA_AES;
  key_info.dwFlags = CRYPT_MACHINE_KEYSET;
  key_info.dwKeySpec = AT_KEYEXCHANGE;

  CERT_BASIC_CONSTRAINTS2_INFO constraints{};
  constraints.fCA = TRUE;
  auto encoded_constraints = ntl::net::detail::encode_tls_object(
      X509_BASIC_CONSTRAINTS2, &constraints);
  BYTE usage_bits =
      static_cast<BYTE>(CERT_KEY_CERT_SIGN_KEY_USAGE | CERT_CRL_SIGN_KEY_USAGE);
  CRYPT_BIT_BLOB usage{sizeof(usage_bits), &usage_bits, 0};
  auto encoded_usage =
      ntl::net::detail::encode_tls_object(X509_KEY_USAGE, &usage);
  std::array<CERT_EXTENSION, 2> extensions{};
  extensions[0] = {const_cast<char *>(szOID_BASIC_CONSTRAINTS2), TRUE,
                   {static_cast<DWORD>(encoded_constraints.size()),
                    encoded_constraints.data()}};
  extensions[1] = {const_cast<char *>(szOID_KEY_USAGE), TRUE,
                   {static_cast<DWORD>(encoded_usage.size()),
                    encoded_usage.data()}};
  CERT_EXTENSIONS certificate_extensions{
      static_cast<DWORD>(extensions.size()), extensions.data()};

  certificate_ = ::CertCreateSelfSignCertificate(
      static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_), &name, 0,
      &key_info, &signature, &start, &end, &certificate_extensions);
  if (!certificate_)
    fail("CertCreateSelfSignCertificate(kernel browser CA)");
}

ephemeral_authority::~ephemeral_authority() {
  if (certificate_)
    (void)::CertFreeCertificateContext(certificate_);
  if (key_)
    (void)::CryptDestroyKey(key_);
  if (provider_)
    (void)::CryptReleaseContext(provider_, 0);
  if (!container_name_.empty()) {
    HCRYPTPROV deleted = 0;
    (void)::CryptAcquireContextW(
        &deleted, container_name_.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
        CRYPT_DELETEKEYSET | CRYPT_MACHINE_KEYSET | CRYPT_SILENT);
  }
}

void ephemeral_authority::export_public_certificate(
    const std::filesystem::path &path) const {
  if (!certificate_)
    throw std::logic_error("kernel browser CA is unavailable");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create kernel browser CA file");
  output.write(reinterpret_cast<const char *>(certificate_->pbCertEncoded),
               static_cast<std::streamsize>(certificate_->cbCertEncoded));
  if (!output)
    throw std::runtime_error("cannot write kernel browser CA file");
}

} // namespace crtsys::wfp_kernel_browser_https
