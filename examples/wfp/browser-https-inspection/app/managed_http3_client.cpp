#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/net/http3/msh3_client>

namespace {

constexpr std::size_t maximum_response_bytes =
    4 * 1024 * 1024;

class certificate_context {
public:
  certificate_context() noexcept = default;
  explicit certificate_context(
      PCCERT_CONTEXT value) noexcept
      : value_(value) {}
  certificate_context(const certificate_context &) = delete;
  certificate_context &
  operator=(const certificate_context &) = delete;
  certificate_context(
      certificate_context &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  certificate_context &
  operator=(certificate_context &&other) noexcept {
    if (this != &other) {
      if (value_)
        (void)::CertFreeCertificateContext(value_);
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }
  ~certificate_context() {
    if (value_)
      (void)::CertFreeCertificateContext(value_);
  }

  PCCERT_CONTEXT get() const noexcept { return value_; }

private:
  PCCERT_CONTEXT value_ = nullptr;
};

[[noreturn]] void throw_last_error(
    std::string_view operation) {
  const DWORD error = ::GetLastError();
  throw std::system_error(
      static_cast<int>(error),
      std::system_category(),
      std::string(operation) +
          " (Win32 " + std::to_string(error) + ")");
}

struct parsed_url {
  std::wstring host;
  std::wstring target;
  INTERNET_PORT port = 0;
};

parsed_url parse_https_url(std::wstring_view text) {
  if (text.empty() ||
      text.size() >
          (std::numeric_limits<DWORD>::max)())
    throw std::invalid_argument("invalid HTTPS URL");
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength =
      (std::numeric_limits<DWORD>::max)();
  parts.dwHostNameLength =
      (std::numeric_limits<DWORD>::max)();
  parts.dwUrlPathLength =
      (std::numeric_limits<DWORD>::max)();
  parts.dwExtraInfoLength =
      (std::numeric_limits<DWORD>::max)();
  if (!::WinHttpCrackUrl(
          text.data(), static_cast<DWORD>(text.size()),
          0, &parts))
    throw_last_error("WinHttpCrackUrl");
  if (parts.nScheme != INTERNET_SCHEME_HTTPS ||
      parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
      !parts.lpszHostName || parts.dwHostNameLength == 0)
    throw std::invalid_argument(
        "managed HTTP/3 client requires HTTPS port 443");

  parsed_url result;
  result.host.assign(
      parts.lpszHostName, parts.dwHostNameLength);
  result.port = parts.nPort;
  if (parts.lpszUrlPath && parts.dwUrlPathLength)
    result.target.assign(
        parts.lpszUrlPath, parts.dwUrlPathLength);
  else
    result.target = L"/";
  if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
    result.target.append(
        parts.lpszExtraInfo, parts.dwExtraInfoLength);
  return result;
}

std::string narrow_ascii(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character <= 0 || character > 0x7f)
      throw std::invalid_argument(
          "managed HTTP/3 URL must be ASCII or punycode");
    result.push_back(static_cast<char>(character));
  }
  return result;
}

ntl::net::http3::msh3_client::response fetch_http3(
    const parsed_url &url,
    std::optional<std::uint16_t> inspection_port,
    PCCERT_CONTEXT inspection_authority) {
  ntl::net::http3::msh3_client::request request;
  request.server_name = narrow_ascii(url.host);
  request.port = url.port;
  if (inspection_port)
    request.peer =
        ntl::net::http3::msh3_client::peer_endpoint::
            ipv4_loopback(*inspection_port);
  request.method = "GET";
  request.path = narrow_ascii(url.target);
  request.headers.push_back(
      {"user-agent", "crtsys-ntl-managed-http3-client/1.0"});

  ntl::net::http3::msh3_client::client_limits limits;
  limits.maximum_response_body_bytes =
      maximum_response_bytes;
  limits.response_timeout = std::chrono::seconds(70);
  auto response = [&] {
    if (inspection_authority) {
      ntl::net::http3::msh3_client::private_ca_client client(
          inspection_authority, limits);
      return client.send(request);
    }
    ntl::net::http3::msh3_client::system_trust_client client(
        limits);
    return client.send(request);
  }();
  if (!response) {
    const auto status =
        static_cast<NTSTATUS>(response.status());
    std::ostringstream operation;
    operation
        << "NTL msh3 HTTP/3 request (status 0x"
        << std::hex << std::uppercase
        << static_cast<std::uint32_t>(status) << ')';
    throw std::system_error(
        static_cast<int>(status),
        std::system_category(),
        operation.str());
  }
  return std::move(*response);
}

certificate_context load_certificate(
    const std::filesystem::path &path) {
  const std::uintmax_t size =
      std::filesystem::file_size(path);
  if (size == 0 || size > 1024 * 1024 ||
      size > (std::numeric_limits<DWORD>::max)())
    throw std::invalid_argument(
        "inspection CA file has an invalid size");
  std::vector<BYTE> encoded(
      static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  if (!input ||
      !input.read(
          reinterpret_cast<char *>(encoded.data()),
          static_cast<std::streamsize>(encoded.size())))
    throw std::runtime_error(
        "cannot read inspection CA certificate");
  PCCERT_CONTEXT certificate =
      ::CertCreateCertificateContext(
          X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
          encoded.data(),
          static_cast<DWORD>(encoded.size()));
  if (!certificate)
    throw_last_error(
        "CertCreateCertificateContext(inspection CA)");
  return certificate_context(certificate);
}

std::uint16_t parse_port(std::wstring_view text) {
  const std::string value = narrow_ascii(text);
  unsigned port = 0;
  const auto parsed = std::from_chars(
      value.data(), value.data() + value.size(), port);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() ||
      port == 0 || port > 65535)
    throw std::invalid_argument(
        "inspection port must be between 1 and 65535");
  return static_cast<std::uint16_t>(port);
}

void write_body(
    const std::filesystem::path &path,
    const std::vector<std::byte> &body) {
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error(
        "cannot create managed client output");
  output.write(
      reinterpret_cast<const char *>(body.data()),
      static_cast<std::streamsize>(body.size()));
  if (!output)
    throw std::runtime_error(
        "cannot write managed client output");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3 && argc != 5)
      throw std::invalid_argument(
          "usage: crtsys_ntl_managed_http3_client.exe "
          "<https-url> <output-file> "
          "[<inspection-port> <inspection-ca.cer>]\n"
          "   or: <https-url> <output-file> "
          "--trust-ca <inspection-ca.cer>");
    const parsed_url url = parse_https_url(argv[1]);
    const bool transparent_private_ca =
        argc == 5 &&
        std::wstring_view(argv[3]) == L"--trust-ca";
    const std::optional<std::uint16_t> inspection_port =
        argc == 5 && !transparent_private_ca
            ? std::optional<std::uint16_t>(
                  parse_port(argv[3]))
            : std::nullopt;
    certificate_context inspection_authority;
    if (argc == 5)
      inspection_authority =
          load_certificate(argv[4]);
    auto response = fetch_http3(
        url, inspection_port,
        inspection_authority.get());
    write_body(argv[2], response.body);
    std::wcout
        << L"NTL managed HTTP/3 client passed: host="
        << url.host << L", protocol=h3, status="
        << response.status << L", bytes="
        << response.body.size() << L", trust="
        << (inspection_authority.get()
                ? L"private-ca"
                : L"windows-system")
        << L", peer="
        << (inspection_port ? L"explicit-loopback"
                            : L"original-destination")
        << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr
        << "NTL managed HTTP/3 client failed: "
        << error.what() << '\n';
    return 1;
  }
}
