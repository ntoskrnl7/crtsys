#pragma once

#include <charconv>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ntl/net/http/http1_framing>
#include <ntl/net/tls/framed_stream>

namespace crtsys::wfp_sample {

constexpr std::size_t maximum_http_header_size = 32 * 1024;
constexpr std::size_t maximum_http_body_size = 2 * 1024 * 1024;
constexpr std::size_t maximum_http_message_size =
    maximum_http_header_size + maximum_http_body_size + 256 * 1024;
inline bool ascii_equal_ci(std::string_view left,
                    std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    const auto lower = [](unsigned char value) noexcept {
      return value >= 'A' && value <= 'Z'
                 ? static_cast<unsigned char>(
                       value + ('a' - 'A'))
                 : value;
    };
    if (lower(static_cast<unsigned char>(left[index])) !=
        lower(static_cast<unsigned char>(right[index])))
      return false;
  }
  return true;
}

inline bool ascii_contains_ci(std::string_view value,
                       std::string_view pattern) noexcept {
  if (pattern.empty())
    return true;
  if (pattern.size() > value.size())
    return false;
  for (std::size_t offset = 0;
       offset <= value.size() - pattern.size(); ++offset) {
    if (ascii_equal_ci(
            value.substr(offset, pattern.size()), pattern))
      return true;
  }
  return false;
}

inline std::string_view trim_http_ows(std::string_view value) noexcept {
  while (!value.empty() &&
         (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

inline std::vector<std::byte>
decode_chunked_body(std::span<const std::byte> encoded) {
  const std::string_view text(
      reinterpret_cast<const char *>(encoded.data()),
      encoded.size());
  std::vector<std::byte> decoded;
  std::size_t position = 0;
  for (;;) {
    const std::size_t line_end = text.find("\r\n", position);
    if (line_end == std::string_view::npos)
      throw std::runtime_error(
          "validated chunked body lost its size line");
    std::string_view size_text =
        text.substr(position, line_end - position);
    const std::size_t extension = size_text.find(';');
    if (extension != std::string_view::npos)
      size_text = size_text.substr(0, extension);
    if (size_text.empty())
      throw std::runtime_error("empty HTTP chunk size");
    std::size_t chunk_size = 0;
    const auto conversion = std::from_chars(
        size_text.data(), size_text.data() + size_text.size(),
        chunk_size, 16);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != size_text.data() + size_text.size())
      throw std::runtime_error("invalid HTTP chunk size");
    position = line_end + 2;
    if (chunk_size == 0)
      return decoded;
    if (chunk_size > maximum_http_body_size - decoded.size() ||
        position > text.size() ||
        chunk_size > text.size() - position ||
        text.size() - position - chunk_size < 2 ||
        text[position + chunk_size] != '\r' ||
        text[position + chunk_size + 1] != '\n')
      throw std::runtime_error(
          "invalid or oversized HTTP chunk data");
    const auto *first = encoded.data() + position;
    decoded.insert(decoded.end(), first, first + chunk_size);
    position += chunk_size + 2;
  }
}

struct parsed_http_response {
  unsigned status = 0;
  std::string location;
  std::string content_type;
  std::string content_encoding;
  std::string connection;
  std::string upgrade;
  std::string websocket_extensions;
  std::vector<std::byte> body;
  std::size_t wire_size = 0;
  bool body_decoded = true;

  bool websocket_upgrade() const noexcept {
    return status == 101 &&
           ascii_equal_ci(upgrade, "websocket") &&
           ascii_contains_ci(connection, "upgrade");
  }
};

inline parsed_http_response parse_http_response(
    const ntl::net::framed_message &message) {
  const auto wire = message.frame();
  const std::string_view text(
      reinterpret_cast<const char *>(wire.data()), wire.size());
  const std::size_t header_end = text.find("\r\n\r\n");
  const std::size_t status_end = text.find("\r\n");
  if (header_end == std::string_view::npos ||
      status_end == std::string_view::npos ||
      status_end < 12 || text.substr(0, 5) != "HTTP/")
    throw std::runtime_error("invalid framed HTTP response");

  const std::size_t first_space = text.find(' ');
  if (first_space == std::string_view::npos ||
      first_space + 4 > status_end)
    throw std::runtime_error("HTTP response has no status code");
  unsigned status = 0;
  const auto status_conversion = std::from_chars(
      text.data() + first_space + 1,
      text.data() + first_space + 4, status);
  if (status_conversion.ec != std::errc{} ||
      status_conversion.ptr != text.data() + first_space + 4)
    throw std::runtime_error("invalid HTTP response status");

  parsed_http_response result;
  result.status = status;
  result.wire_size = wire.size();
  bool chunked = false;
  std::size_t line = status_end + 2;
  while (line < header_end) {
    const std::size_t line_end = text.find("\r\n", line);
    if (line_end == std::string_view::npos ||
        line_end > header_end)
      throw std::runtime_error("invalid HTTP response header");
    const std::size_t colon = text.find(':', line);
    if (colon == std::string_view::npos || colon >= line_end)
      throw std::runtime_error("invalid HTTP response field");
    const std::string_view name =
        text.substr(line, colon - line);
    const std::string_view value = trim_http_ows(
        text.substr(colon + 1, line_end - colon - 1));
    if (ascii_equal_ci(name, "location"))
      result.location.assign(value);
    else if (ascii_equal_ci(name, "content-type"))
      result.content_type.assign(value);
    else if (ascii_equal_ci(name, "content-encoding"))
      result.content_encoding.assign(value);
    else if (ascii_equal_ci(name, "connection"))
      result.connection.assign(value);
    else if (ascii_equal_ci(name, "upgrade"))
      result.upgrade.assign(value);
    else if (ascii_equal_ci(name, "sec-websocket-extensions"))
      result.websocket_extensions.assign(value);
    else if (ascii_equal_ci(name, "transfer-encoding"))
      chunked = ascii_contains_ci(value, "chunked");
    line = line_end + 2;
  }

  const auto encoded_body = wire.subspan(header_end + 4);
  if (chunked)
    result.body = decode_chunked_body(encoded_body);
  else
    result.body.assign(encoded_body.begin(), encoded_body.end());
  result.body_decoded =
      result.content_encoding.empty() ||
      ascii_equal_ci(result.content_encoding, "identity");
  return result;
}

inline ntl::net::http::http1_message_framer
make_http_framer(
    ntl::net::http::http1_message_kind kind,
    bool allow_close_delimited_response = false,
    bool response_body_forbidden = false) {
  return ntl::net::http::http1_message_framer(
      kind,
      {.maximum_header_size = maximum_http_header_size,
       .maximum_body_size = maximum_http_body_size,
       .maximum_chunk_line_size = 4 * 1024,
       .maximum_trailer_size = 16 * 1024,
       .allow_close_delimited_response =
           allow_close_delimited_response,
       .response_body_forbidden =
           response_body_forbidden});
}

} // namespace crtsys::wfp_sample
