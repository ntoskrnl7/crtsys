#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <array>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "runtime_controller_fixture.hpp"

namespace crtsys::test::wfp::tls_fixture {

class certificate_context {
public:
  certificate_context() noexcept = default;
  explicit certificate_context(PCCERT_CONTEXT value) noexcept
      : value_(value) {}
  certificate_context(const certificate_context &) = delete;
  certificate_context &operator=(const certificate_context &) = delete;
  certificate_context(certificate_context &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  certificate_context &operator=(certificate_context &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }
  ~certificate_context() { reset(); }
  PCCERT_CONTEXT get() const noexcept { return value_; }

private:
  void reset() noexcept {
    if (value_)
      (void)::CertFreeCertificateContext(value_);
    value_ = nullptr;
  }
  PCCERT_CONTEXT value_ = nullptr;
};

inline certificate_context load_der_certificate(
    const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("TLS fixture CA file is missing");
  const std::vector<unsigned char> bytes(
      std::istreambuf_iterator<char>(input), {});
  if (bytes.empty() || bytes.size() > MAXDWORD)
    throw std::runtime_error("TLS fixture CA file is invalid");
  auto *certificate = ::CertCreateCertificateContext(
      X509_ASN_ENCODING, bytes.data(), static_cast<DWORD>(bytes.size()));
  if (!certificate)
    throw std::system_error(::GetLastError(), std::system_category(),
                            "CertCreateCertificateContext(fixture CA)");
  return certificate_context(certificate);
}

using certificate_thumbprint = std::array<std::byte, 20>;

inline certificate_thumbprint load_certificate_thumbprint(
    const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("TLS fixture identity thumbprint is missing");
  certificate_thumbprint result{};
  input.read(reinterpret_cast<char *>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (input.gcount() != static_cast<std::streamsize>(result.size()) ||
      input.peek() != std::ifstream::traits_type::eof())
    throw std::runtime_error("TLS fixture identity thumbprint is invalid");
  return result;
}

class machine_certificate {
public:
  machine_certificate(const certificate_thumbprint &thumbprint,
                      std::wstring_view store_name = L"MY") {
    const std::wstring name(store_name);
    store_ = ::CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG |
            CERT_STORE_READONLY_FLAG,
        name.c_str());
    if (!store_)
      throw std::system_error(::GetLastError(), std::system_category(),
                              "CertOpenStore(fixture identity)");
    CRYPT_HASH_BLOB query{
        static_cast<DWORD>(thumbprint.size()),
        reinterpret_cast<BYTE *>(
            const_cast<std::byte *>(thumbprint.data()))};
    certificate_ = ::CertFindCertificateInStore(
        store_, X509_ASN_ENCODING, 0, CERT_FIND_HASH, &query, nullptr);
    if (!certificate_)
      throw std::runtime_error(
          "TLS fixture exact identity was not published");
    DWORD size = 0;
    if (!::CertGetCertificateContextProperty(
            certificate_, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) ||
        size == 0)
      throw std::runtime_error("TLS fixture identity has no private key");
  }
  machine_certificate(const machine_certificate &) = delete;
  machine_certificate &operator=(const machine_certificate &) = delete;
  ~machine_certificate() {
    if (certificate_)
      (void)::CertFreeCertificateContext(certificate_);
    if (store_)
      (void)::CertCloseStore(store_, 0);
  }
  PCCERT_CONTEXT get() const noexcept { return certificate_; }

private:
  HCERTSTORE store_ = nullptr;
  PCCERT_CONTEXT certificate_ = nullptr;
};

class controller_process {
public:
  using option = std::pair<std::wstring, std::wstring>;

  controller_process(const std::filesystem::path &executable,
                     const std::filesystem::path &state,
                     const std::vector<option> &options,
                     std::uint32_t duration_ms = 60'000)
      : ready_(state / "ready"),
        remove_policy_(state / "remove-policy"),
        policy_removed_(state / "policy-removed"),
        stop_(state / "stop"), stats_(state / "stats"),
        ca_(state / "authority.cer"),
        identity_thumbprint_(state / "identity.sha1") {
    if (!std::filesystem::is_regular_file(executable))
      throw std::invalid_argument("TLS service executable does not exist");
    std::wstring command =
        crtsys::test::wfp::runtime_fixture::quote(executable.wstring());
    for (const auto &[name, value] : options)
      command += L" " + name + L" " +
                 crtsys::test::wfp::runtime_fixture::quote(value);
    const auto quote = crtsys::test::wfp::runtime_fixture::quote;
    command += L" --ready-file " + quote(ready_.wstring()) +
               L" --remove-policy-file " + quote(remove_policy_.wstring()) +
               L" --policy-removed-file " + quote(policy_removed_.wstring()) +
               L" --stop-file " + quote(stop_.wstring()) +
               L" --stats-file " + quote(stats_.wstring()) +
               L" --ca-file " + quote(ca_.wstring()) +
               L" --identity-thumbprint-file " +
                   quote(identity_thumbprint_.wstring()) +
               L" --duration-ms " + std::to_wstring(duration_ms);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working_directory = executable.parent_path();
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr,
                          working_directory.c_str(), &startup, &process))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "CreateProcessW(TLS service)");
    process_ = process.hProcess;
    thread_ = process.hThread;
  }

  controller_process(const controller_process &) = delete;
  controller_process &operator=(const controller_process &) = delete;
  ~controller_process() {
    if (process_ && !completed_) {
      (void)::TerminateProcess(process_, ERROR_CANCELLED);
      (void)::WaitForSingleObject(process_, 5000);
    }
    if (thread_)
      ::CloseHandle(thread_);
    if (process_)
      ::CloseHandle(process_);
  }

  void wait_ready(std::uint32_t timeout_ms = 30'000) {
    wait_file(ready_, timeout_ms);
    wait_file(ca_, timeout_ms);
    wait_file(identity_thumbprint_, timeout_ms);
  }
  void request_policy_removal() const { signal(remove_policy_); }
  void wait_policy_removed(std::uint32_t timeout_ms = 15'000) {
    wait_file(policy_removed_, timeout_ms);
  }
  void request_stop() const { signal(stop_); }
  void wait(std::uint32_t timeout_ms = 15'000) {
    const DWORD waited = ::WaitForSingleObject(process_, timeout_ms);
    if (waited != WAIT_OBJECT_0)
      throw std::runtime_error("TLS service exit timeout");
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (!::GetExitCodeProcess(process_, &exit_code))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "GetExitCodeProcess(TLS service)");
    completed_ = true;
    if (exit_code != 0)
      throw std::runtime_error("TLS service reported failure");
  }

  const std::filesystem::path &ca_file() const noexcept { return ca_; }
  const std::filesystem::path &identity_thumbprint_file() const noexcept {
    return identity_thumbprint_;
  }
  const std::filesystem::path &stats_file() const noexcept { return stats_; }

private:
  void signal(const std::filesystem::path &path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("cannot signal TLS service");
    output << "signal\n";
  }
  void wait_file(const std::filesystem::path &path,
                 std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (!std::filesystem::exists(path)) {
      if (::WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
        throw std::runtime_error("TLS service exited before IPC signal");
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("TLS service IPC timeout");
      ::Sleep(20);
    }
  }

  std::filesystem::path ready_;
  std::filesystem::path remove_policy_;
  std::filesystem::path policy_removed_;
  std::filesystem::path stop_;
  std::filesystem::path stats_;
  std::filesystem::path ca_;
  std::filesystem::path identity_thumbprint_;
  HANDLE process_ = nullptr;
  HANDLE thread_ = nullptr;
  bool completed_ = false;
};

} // namespace crtsys::test::wfp::tls_fixture
