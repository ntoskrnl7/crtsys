#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include "http3_origin.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/net/tls/inspection_frontend>

namespace crtsys::wfp_sample::browser_https {
namespace {

constexpr std::size_t maximum_request_header_bytes =
    32 * 1024;
constexpr std::size_t maximum_response_body_bytes =
    4 * 1024 * 1024;
constexpr std::size_t maximum_response_headers = 256;
constexpr std::size_t maximum_response_header_bytes =
    48 * 1024;

class winhttp_handle {
public:
  winhttp_handle() noexcept = default;
  explicit winhttp_handle(HINTERNET value) noexcept
      : value_(value) {}
  winhttp_handle(const winhttp_handle &) = delete;
  winhttp_handle &operator=(const winhttp_handle &) = delete;
  winhttp_handle(winhttp_handle &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  winhttp_handle &operator=(winhttp_handle &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }
  ~winhttp_handle() { reset(); }

  HINTERNET get() const noexcept { return value_; }
  explicit operator bool() const noexcept {
    return value_ != nullptr;
  }

private:
  void reset() noexcept {
    if (value_)
      (void)::WinHttpCloseHandle(value_);
    value_ = nullptr;
  }

  HINTERNET value_ = nullptr;
};

[[noreturn]] void throw_winhttp(
    std::string_view operation) {
  throw std::system_error(
      static_cast<int>(::GetLastError()),
      std::system_category(), std::string(operation));
}

std::wstring widen_ascii(std::string_view value) {
  std::wstring result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    if (character == 0 || character > 0x7f)
      throw std::invalid_argument(
          "HTTP/3 origin accepts ASCII fields only");
    result.push_back(static_cast<wchar_t>(character));
  }
  return result;
}

std::string narrow_ascii(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character <= 0 || character > 0x7f)
      throw std::runtime_error(
          "upstream HTTP header is not ASCII");
    result.push_back(static_cast<char>(character));
  }
  return result;
}

std::wstring query_winhttp_header(
    HINTERNET request, DWORD query) {
  DWORD bytes = 0;
  if (::WinHttpQueryHeaders(
          request, query, WINHTTP_HEADER_NAME_BY_INDEX,
          nullptr, &bytes, WINHTTP_NO_HEADER_INDEX))
    return {};
  const DWORD error = ::GetLastError();
  if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
    return {};
  if (error != ERROR_INSUFFICIENT_BUFFER)
    throw_winhttp("WinHttpQueryHeaders(size)");
  std::vector<wchar_t> buffer(
      bytes / sizeof(wchar_t) + 1, L'\0');
  if (!::WinHttpQueryHeaders(
          request, query, WINHTTP_HEADER_NAME_BY_INDEX,
          buffer.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
    throw_winhttp("WinHttpQueryHeaders");
  return std::wstring(buffer.data());
}

bool is_http_token_character(
    unsigned char character) noexcept {
  return
      (character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z') ||
      (character >= '0' && character <= '9') ||
      std::string_view("!#$%&'*+-.^_`|~").find(
          static_cast<char>(character)) !=
          std::string_view::npos;
}

bool is_hop_by_hop_header(std::string_view name) {
  return ascii_equal_ci(name, "connection") ||
         ascii_equal_ci(name, "keep-alive") ||
         ascii_equal_ci(name, "proxy-authenticate") ||
         ascii_equal_ci(name, "proxy-authorization") ||
         ascii_equal_ci(name, "proxy-connection") ||
         ascii_equal_ci(name, "transfer-encoding") ||
         ascii_equal_ci(name, "upgrade");
}

std::wstring make_upstream_request_headers(
    const http3_header_fields &headers) {
  std::wstring result;
  result.reserve(maximum_request_header_bytes);
  for (const auto &[name, value] : headers) {
    if (name.empty() || name.front() == ':' ||
        is_hop_by_hop_header(name) ||
        ascii_equal_ci(name, "host") ||
        ascii_equal_ci(name, "content-length") ||
        ascii_equal_ci(name, "accept-encoding") ||
        ascii_equal_ci(name, "te"))
      continue;
    if (!std::all_of(
            name.begin(), name.end(),
            [](unsigned char character) {
              return is_http_token_character(character);
            }) ||
        value.find_first_of("\r\n") != std::string::npos)
      throw std::invalid_argument(
          "HTTP/3 request contains an invalid header");
    result += widen_ascii(name);
    result += L": ";
    result += widen_ascii(value);
    result += L"\r\n";
  }
  result += L"Accept-Encoding: identity\r\n";
  if (result.size() > maximum_request_header_bytes)
    throw std::length_error(
        "forwarded HTTP request headers exceed the bounded limit");
  return result;
}

http3_header_fields parse_upstream_response_headers(
    std::wstring_view raw_headers) {
  http3_header_fields result;
  std::size_t bytes = 0;
  std::size_t offset = raw_headers.find(L"\r\n");
  if (offset == std::wstring_view::npos)
    throw std::runtime_error(
        "upstream HTTP response has no status line");
  offset += 2;
  while (offset < raw_headers.size()) {
    const std::size_t end =
        raw_headers.find(L"\r\n", offset);
    if (end == std::wstring_view::npos)
      throw std::runtime_error(
          "upstream HTTP response headers are truncated");
    if (end == offset)
      break;
    const std::wstring_view line =
        raw_headers.substr(offset, end - offset);
    const std::size_t colon = line.find(L':');
    if (colon == std::wstring_view::npos || colon == 0)
      throw std::runtime_error(
          "upstream HTTP response header is malformed");
    std::string name = narrow_ascii(line.substr(0, colon));
    for (char &character : name) {
      if (character >= 'A' && character <= 'Z')
        character = static_cast<char>(
            character - 'A' + 'a');
    }
    if (!std::all_of(
            name.begin(), name.end(),
            [](unsigned char character) {
              return is_http_token_character(character);
            }))
      throw std::runtime_error(
          "upstream HTTP response header name is invalid");
    std::size_t value_start = colon + 1;
    while (value_start < line.size() &&
           (line[value_start] == L' ' ||
            line[value_start] == L'\t'))
      ++value_start;
    std::string value =
        narrow_ascii(line.substr(value_start));
    if (!is_hop_by_hop_header(name) &&
        !ascii_equal_ci(name, "content-length") &&
        !ascii_equal_ci(name, "alt-svc")) {
      bytes += name.size() + value.size();
      if (result.size() >= maximum_response_headers ||
          bytes > maximum_response_header_bytes)
        throw std::length_error(
            "upstream HTTP response headers exceed the bounded limit");
      result.emplace_back(
          std::move(name), std::move(value));
    }
    offset = end + 2;
  }
  return result;
}

} // namespace

static http3_origin_response fetch_origin_winhttp(
    std::wstring_view server_name,
    std::string_view method,
    std::string_view path,
    const http3_header_fields &headers,
    std::span<const std::byte> body,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities,
    bool require_http3) {
  if (method.empty() ||
      ascii_equal_ci(method, "CONNECT") ||
      !std::all_of(
          method.begin(), method.end(),
          [](unsigned char character) {
            return is_http_token_character(character);
          }))
    throw std::invalid_argument(
        "HTTP/3 request method is invalid or unsupported");
  if (path.empty() || path.front() != '/' ||
      path.find_first_of("\r\n") != std::string_view::npos)
    throw std::invalid_argument(
        "HTTP/3 request path is invalid");

  const std::wstring host(server_name);
  const std::wstring verb = widen_ascii(method);
  const std::wstring target = widen_ascii(path);
  winhttp_handle session(::WinHttpOpen(
      L"crtsys-ntl-http3-inspection/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session)
    throw_winhttp("WinHttpOpen");
  const DWORD timeout_ms = 30000;
  if (!::WinHttpSetTimeouts(
          session.get(), timeout_ms, timeout_ms,
          timeout_ms, timeout_ms))
    throw_winhttp("WinHttpSetTimeouts");

  winhttp_handle connection(::WinHttpConnect(
      session.get(), host.c_str(),
      INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection)
    throw_winhttp("WinHttpConnect");
  winhttp_handle request(::WinHttpOpenRequest(
      connection.get(), verb.c_str(), target.c_str(),
      nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request)
    throw_winhttp("WinHttpOpenRequest");

  auto client_identity = origin_identities.select(
      {.server_name = server_name,
       .acceptable_issuers = {}});
  if (!client_identity)
    throw std::system_error(
        static_cast<int>(
            static_cast<NTSTATUS>(
                client_identity.status())),
        std::system_category(),
        "HTTP/3 origin mTLS identity selection");
  if (*client_identity &&
      !::WinHttpSetOption(
          request.get(),
          WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
          const_cast<CERT_CONTEXT *>(
              client_identity->get()),
          sizeof(CERT_CONTEXT)))
    throw_winhttp(
        "WinHttpSetOption(client certificate)");

  DWORD enabled_protocols =
      require_http3
          ? WINHTTP_PROTOCOL_FLAG_HTTP3
          : WINHTTP_PROTOCOL_FLAG_HTTP2;
  if (!::WinHttpSetOption(
          request.get(),
          WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,
          &enabled_protocols,
          sizeof(enabled_protocols)))
    throw_winhttp(
        "WinHttpSetOption(enable HTTP/3)");
  if (require_http3) {
    BOOL protocol_required = TRUE;
    if (!::WinHttpSetOption(
            request.get(),
            WINHTTP_OPTION_HTTP_PROTOCOL_REQUIRED,
            &protocol_required, sizeof(protocol_required)))
      throw_winhttp(
          "WinHttpSetOption(require HTTP/3)");
  }

  DWORD disabled_features = WINHTTP_DISABLE_REDIRECTS;
  if (!::WinHttpSetOption(
          request.get(), WINHTTP_OPTION_DISABLE_FEATURE,
          &disabled_features, sizeof(disabled_features)))
    throw_winhttp("WinHttpSetOption(disable redirects)");
  const std::wstring request_headers =
      make_upstream_request_headers(headers);
  if (body.size() >
      (std::numeric_limits<DWORD>::max)())
    throw std::length_error(
        "HTTP/3 request body is too large for WinHTTP");
  const DWORD body_size = static_cast<DWORD>(body.size());
  if (!::WinHttpSendRequest(
          request.get(), request_headers.c_str(),
          static_cast<DWORD>(request_headers.size()),
          body.empty() ? WINHTTP_NO_REQUEST_DATA
                       : const_cast<std::byte *>(body.data()),
          body_size, body_size, 0))
    throw_winhttp("WinHttpSendRequest");
  if (!::WinHttpReceiveResponse(request.get(), nullptr))
    throw_winhttp("WinHttpReceiveResponse");

  http3_origin_response result;
  DWORD used_protocol = 0;
  DWORD used_protocol_size = sizeof(used_protocol);
  if (!::WinHttpQueryOption(
          request.get(),
          WINHTTP_OPTION_HTTP_PROTOCOL_USED,
          &used_protocol, &used_protocol_size))
    throw_winhttp(
        "WinHttpQueryOption(HTTP protocol used)");
  if (require_http3 &&
      (used_protocol & WINHTTP_PROTOCOL_FLAG_HTTP3) == 0)
    throw std::runtime_error(
        "origin did not negotiate required HTTP/3");
  result.negotiated_protocol =
      (used_protocol & WINHTTP_PROTOCOL_FLAG_HTTP3) != 0
          ? "h3"
          : (used_protocol & WINHTTP_PROTOCOL_FLAG_HTTP2) != 0
                ? "h2"
                : "http/1.1";
  DWORD status = 0;
  DWORD status_bytes = sizeof(status);
  if (!::WinHttpQueryHeaders(
          request.get(),
          WINHTTP_QUERY_STATUS_CODE |
              WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status,
          &status_bytes, WINHTTP_NO_HEADER_INDEX))
    throw_winhttp("WinHttpQueryHeaders(status)");
  result.message.status = status;
  result.message.content_type = narrow_ascii(
      query_winhttp_header(
          request.get(), WINHTTP_QUERY_CONTENT_TYPE));
  result.message.content_encoding = narrow_ascii(
      query_winhttp_header(
          request.get(), WINHTTP_QUERY_CONTENT_ENCODING));
  result.message.location = narrow_ascii(
      query_winhttp_header(
          request.get(), WINHTTP_QUERY_LOCATION));
  result.headers = parse_upstream_response_headers(
      query_winhttp_header(
          request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF));

  if (!ascii_equal_ci(method, "HEAD")) {
    for (;;) {
      DWORD available = 0;
      if (!::WinHttpQueryDataAvailable(
              request.get(), &available))
        throw_winhttp("WinHttpQueryDataAvailable");
      if (available == 0)
        break;
      if (available >
          maximum_response_body_bytes -
              result.message.body.size())
        throw std::length_error(
            "upstream HTTP response exceeds the bounded limit");
      const std::size_t offset =
          result.message.body.size();
      result.message.body.resize(offset + available);
      DWORD received = 0;
      if (!::WinHttpReadData(
              request.get(),
              result.message.body.data() + offset,
              available, &received))
        throw_winhttp("WinHttpReadData");
      result.message.body.resize(offset + received);
      if (received == 0)
        break;
    }
  }
  result.message.wire_size = result.message.body.size();
  result.message.body_decoded =
      result.message.content_encoding.empty() ||
      ascii_equal_ci(
          result.message.content_encoding, "identity");
  return result;
}

http3_origin_response fetch_http3_origin_winhttp(
    std::wstring_view server_name,
    std::string_view method,
    std::string_view path,
    const http3_header_fields &headers,
    std::span<const std::byte> body,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities) {
  return fetch_origin_winhttp(
      server_name, method, path, headers, body,
      origin_identities, true);
}

http3_origin_response
fetch_http_origin_with_transport_fallback_winhttp(
    std::wstring_view server_name,
    std::string_view method,
    std::string_view path,
    const http3_header_fields &headers,
    std::span<const std::byte> body,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities) {
  try {
    return fetch_origin_winhttp(
        server_name, method, path, headers, body,
        origin_identities, true);
  } catch (const std::system_error &error) {
    const std::uint32_t code =
        static_cast<std::uint32_t>(
            error.code().value());
    const bool quic_transport_failure =
        (code & 0xffff0000u) == 0x80410000u ||
        code == ERROR_WINHTTP_TIMEOUT ||
        code == ERROR_WINHTTP_CANNOT_CONNECT ||
        code == ERROR_WINHTTP_CONNECTION_ERROR;
    if (!quic_transport_failure)
      throw;
  }
  return fetch_origin_winhttp(
      server_name, method, path, headers, body,
      origin_identities, false);
}

} // namespace crtsys::wfp_sample::browser_https
