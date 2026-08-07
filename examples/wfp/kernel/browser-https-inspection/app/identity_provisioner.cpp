#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "identity_provisioner.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

bool valid_dns_name(std::string_view value) noexcept {
  if (value.empty() || value.size() > contract::maximum_server_name_size ||
      value.front() == '.' || value.back() == '.')
    return false;
  bool label_start = true;
  for (const unsigned char character : value) {
    if (character == '.') {
      if (label_start)
        return false;
      label_start = true;
      continue;
    }
    if (!(std::isalnum(character) || character == '-'))
      return false;
    if (label_start && character == '-')
      return false;
    label_start = false;
  }
  return !label_start && value.back() != '-';
}

std::string normalize(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return result;
}

} // namespace

identity_provisioner::identity_provisioner(
    configure_callback configure, PCCERT_CONTEXT authority,
    std::filesystem::path audit_directory)
    : configure_(std::move(configure)),
      issuer_(authority,
              {.key_name_prefix = L"crtsys-kernel-browser-identity",
               .rsa_bits = 2048,
               .validity_days = 2,
               .machine_keys = true}),
      audit_directory_(std::filesystem::absolute(
          std::move(audit_directory))) {
  if (!configure_)
    throw std::invalid_argument(
        "identity provisioner requires a configuration callback");
  std::filesystem::create_directories(audit_directory_);
  certificates_.reserve(contract::identity_cache_capacity);
}

std::wstring identity_provisioner::widen_dns_name(std::string_view value) {
  return std::wstring(value.begin(), value.end());
}

bool identity_provisioner::ensure(std::string_view server_name) {
  if (!valid_dns_name(server_name))
    throw std::invalid_argument("kernel browser identity requested invalid SNI");
  std::string normalized = normalize(server_name);
  if (names_.contains(normalized))
    return false;
  if (names_.size() >= contract::identity_cache_capacity)
    throw std::runtime_error("kernel browser identity cache is full");

  provision(normalized, "install");
  names_.insert(std::move(normalized));
  return true;
}

void identity_provisioner::replace(std::string_view server_name) {
  if (!valid_dns_name(server_name))
    throw std::invalid_argument("kernel browser identity replacement is invalid");
  std::string normalized = normalize(server_name);
  if (!names_.contains(normalized)) {
    (void)ensure(normalized);
    return;
  }
  provision(normalized, "replace");
}

void identity_provisioner::provision(std::string_view normalized,
                                     std::string_view operation) {

  auto issued = issuer_.issue(widen_dns_name(normalized));
  auto installed =
      std::make_unique<installed_certificate>(
          issued.borrowed_certificate(), L"MY");
  contract::certificate_config input{};
  input.sha1_thumbprint = installed->thumbprint();
  input.server_name_size = static_cast<std::uint32_t>(normalized.size());
  std::memcpy(input.server_name.data(), normalized.data(), normalized.size());
  input.server_name[normalized.size()] = '\0';

  std::ofstream audit(audit_directory_ / "identities.txt",
                      std::ios::binary | std::ios::app);
  if (!audit)
    throw std::runtime_error("cannot create identity audit log");

  // Keep both the installed certificate and its backing key alive for as long
  // as the driver may acquire or use its Schannel credential.
  certificates_.push_back(
      {std::move(issued), std::move(installed)});
  try {
    configure_(input);
  } catch (...) {
    certificates_.pop_back();
    throw;
  }
  audit << operation << ' ' << normalized << '\n';
}

} // namespace crtsys::wfp_kernel_browser_https
