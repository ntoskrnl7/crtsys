#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <ntl/net/tls/certificate>

#include "windows_support.hpp"

namespace crtsys::wfp_sample {

class ephemeral_certificate {
public:
  explicit ephemeral_certificate(
      bool machine_keys = true)
      : machine_keys_(machine_keys) {
    // GetTickCount64 restarts at every boot.  The old PID/tick name could
    // therefore collide with a partially-created machine keyset left by a
    // terminated acceptance process after a VM reboot.  CAPI can report that
    // damaged collision as ERROR_FILE_NOT_FOUND and leave another unusable
    // container.  Use absolute time plus a process-local sequence and clean a
    // partially-created candidate before retrying.
    DWORD acquire_error = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
      FILETIME now{};
      ::GetSystemTimePreciseAsFileTime(&now);
      const std::uint64_t timestamp =
          (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) |
          now.dwLowDateTime;
      container_name_ =
          L"crtsys-ntl-wfp-tls-" +
          std::to_wstring(::GetCurrentProcessId()) + L"-" +
          std::to_wstring(timestamp) + L"-" +
          std::to_wstring(
              container_sequence_.fetch_add(
                  1, std::memory_order_relaxed));
      const DWORD keyset_flags =
          CRYPT_NEWKEYSET |
          (machine_keys_ ? CRYPT_MACHINE_KEYSET : 0) |
          CRYPT_SILENT;
      if (::CryptAcquireContextW(
              &provider_, container_name_.c_str(),
              MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
              keyset_flags))
        break;

      acquire_error = ::GetLastError();
      HCRYPTPROV deleted = 0;
      (void)::CryptAcquireContextW(
          &deleted, container_name_.c_str(),
          MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
          CRYPT_DELETEKEYSET |
              (machine_keys_ ? CRYPT_MACHINE_KEYSET : 0) |
              CRYPT_SILENT);
      if (acquire_error != ERROR_FILE_NOT_FOUND &&
          acquire_error != NTE_BAD_KEYSET &&
          acquire_error != NTE_EXISTS)
        break;
    }
    if (!provider_) {
      ::SetLastError(acquire_error);
      throw_windows("CryptAcquireContextW(test keyset)");
    }
    if (!::CryptGenKey(
            provider_, AT_KEYEXCHANGE,
            (2048u << 16) | CRYPT_EXPORTABLE, &key_))
      throw_windows("CryptGenKey");

    constexpr wchar_t subject[] =
        L"CN=crtsys NTL WFP HTTPS inspection test CA";
    DWORD encoded_size = 0;
    if (!::CertStrToNameW(
            X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
            nullptr, nullptr, &encoded_size, nullptr))
      throw_windows("CertStrToNameW(size)");
    std::vector<BYTE> encoded(encoded_size);
    if (!::CertStrToNameW(
            X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
            nullptr, encoded.data(), &encoded_size, nullptr))
      throw_windows("CertStrToNameW");

    CERT_NAME_BLOB name{encoded_size, encoded.data()};
    CRYPT_ALGORITHM_IDENTIFIER signature{};
    signature.pszObjId =
        const_cast<char *>(szOID_RSA_SHA256RSA);
    SYSTEMTIME start{};
    ::GetSystemTime(&start);
    SYSTEMTIME end = start;
    ++end.wYear;
    CRYPT_KEY_PROV_INFO key_info{};
    key_info.pwszContainerName = container_name_.data();
    key_info.pwszProvName =
        const_cast<wchar_t *>(MS_ENH_RSA_AES_PROV_W);
    key_info.dwProvType = PROV_RSA_AES;
    key_info.dwFlags =
        machine_keys_ ? CRYPT_MACHINE_KEYSET : 0;
    key_info.dwKeySpec = AT_KEYEXCHANGE;

    CERT_BASIC_CONSTRAINTS2_INFO constraints{};
    constraints.fCA = TRUE;
    auto encoded_constraints =
        ntl::net::detail::encode_tls_object(
            X509_BASIC_CONSTRAINTS2, &constraints);
    BYTE usage_bits = static_cast<BYTE>(
        CERT_KEY_CERT_SIGN_KEY_USAGE |
        CERT_CRL_SIGN_KEY_USAGE);
    CRYPT_BIT_BLOB usage{
        sizeof(usage_bits), &usage_bits, 0};
    auto encoded_usage =
        ntl::net::detail::encode_tls_object(
            X509_KEY_USAGE, &usage);
    std::array<CERT_EXTENSION, 2> extensions{};
    extensions[0] = {
        const_cast<char *>(szOID_BASIC_CONSTRAINTS2), TRUE,
        {static_cast<DWORD>(encoded_constraints.size()),
         encoded_constraints.data()}};
    extensions[1] = {
        const_cast<char *>(szOID_KEY_USAGE), TRUE,
        {static_cast<DWORD>(encoded_usage.size()),
         encoded_usage.data()}};
    CERT_EXTENSIONS certificate_extensions{
        static_cast<DWORD>(extensions.size()),
        extensions.data()};

    certificate_ = ::CertCreateSelfSignCertificate(
        static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
        &name, 0, &key_info, &signature, &start, &end,
        &certificate_extensions);
    if (!certificate_)
      throw_windows("CertCreateSelfSignCertificate");
  }

  ephemeral_certificate(const ephemeral_certificate &) = delete;
  ephemeral_certificate &
  operator=(const ephemeral_certificate &) = delete;

  ~ephemeral_certificate() {
    if (certificate_)
      (void)::CertFreeCertificateContext(certificate_);
    if (key_)
      (void)::CryptDestroyKey(key_);
    if (provider_)
      (void)::CryptReleaseContext(provider_, 0);
    if (!container_name_.empty()) {
      HCRYPTPROV deleted = 0;
      (void)::CryptAcquireContextW(
          &deleted, container_name_.c_str(),
          MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
          CRYPT_DELETEKEYSET |
              (machine_keys_ ? CRYPT_MACHINE_KEYSET : 0) |
              CRYPT_SILENT);
    }
  }

  PCCERT_CONTEXT get() const noexcept { return certificate_; }
  bool machine_keys() const noexcept {
    return machine_keys_;
  }

  void export_public_certificate(
      const std::filesystem::path &path) const {
    if (!certificate_)
      throw std::logic_error(
          "temporary inspection CA is unavailable");
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error(
          "cannot create the inspection CA file");
    output.write(
        reinterpret_cast<const char *>(
            certificate_->pbCertEncoded),
        static_cast<std::streamsize>(
            certificate_->cbCertEncoded));
    if (!output)
      throw std::runtime_error(
          "cannot write the inspection CA file");
  }

private:
  HCRYPTPROV provider_ = 0;
  HCRYPTKEY key_ = 0;
  PCCERT_CONTEXT certificate_ = nullptr;
  std::wstring container_name_;
  bool machine_keys_ = true;
  inline static std::atomic<std::uint64_t> container_sequence_{1};
};

} // namespace crtsys::wfp_sample
