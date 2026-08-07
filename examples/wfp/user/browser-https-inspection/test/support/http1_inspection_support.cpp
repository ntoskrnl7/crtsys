#include "http1_inspection_support.hpp"

#include <cstring>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <ntl/net/buffer/scatter_view>
#include <ntl/net/inspection/core>

namespace crtsys::wfp_sample::browser_https::http1_detail {

rewritten_browser_request rewrite_browser_request(
    std::span<const std::byte> wire) {
  const std::string_view text(
      reinterpret_cast<const char *>(wire.data()), wire.size());
  const std::size_t header_end = text.find("\r\n\r\n");
  const std::size_t request_line_end = text.find("\r\n");
  if (header_end == std::string_view::npos ||
      request_line_end == std::string_view::npos ||
      request_line_end >= header_end)
    throw std::runtime_error(
        "browser sent an invalid framed HTTP request");

  bool connection_upgrade = false;
  bool websocket_upgrade = false;
  std::string websocket_extensions;
  std::size_t probe_line = request_line_end + 2;
  while (probe_line < header_end) {
    const std::size_t line_end =
        text.find("\r\n", probe_line);
    if (line_end == std::string_view::npos ||
        line_end > header_end)
      throw std::runtime_error(
          "browser sent an invalid HTTP request field");
    const std::size_t colon = text.find(':', probe_line);
    if (colon == std::string_view::npos || colon >= line_end)
      throw std::runtime_error(
          "browser sent an invalid HTTP request field");
    const std::string_view name =
        text.substr(probe_line, colon - probe_line);
    const std::string_view value = trim_http_ows(
        text.substr(colon + 1, line_end - colon - 1));
    if (ascii_equal_ci(name, "connection") &&
        ascii_contains_ci(value, "upgrade"))
      connection_upgrade = true;
    if (ascii_equal_ci(name, "upgrade") &&
        ascii_equal_ci(value, "websocket"))
      websocket_upgrade = true;
    if (ascii_equal_ci(name, "sec-websocket-extensions")) {
      if (!websocket_extensions.empty())
        websocket_extensions.append(", ");
      websocket_extensions.append(value);
    }
    probe_line = line_end + 2;
  }
  const bool is_websocket_upgrade =
      connection_upgrade && websocket_upgrade;

  std::string headers;
  headers.reserve(header_end + 64);
  headers.append(text.substr(0, request_line_end + 2));
  std::size_t line = request_line_end + 2;
  while (line < header_end) {
    const std::size_t line_end = text.find("\r\n", line);
    if (line_end == std::string_view::npos ||
        line_end > header_end)
      throw std::runtime_error(
          "browser sent an invalid HTTP request field");
    const std::size_t colon = text.find(':', line);
    if (colon == std::string_view::npos || colon >= line_end)
      throw std::runtime_error(
          "browser sent an invalid HTTP request field");
    const std::string_view name =
        text.substr(line, colon - line);
    const bool remove = ascii_equal_ci(name, "proxy-connection");
    if (!remove)
      headers.append(text.substr(line, line_end - line + 2));
    line = line_end + 2;
  }
  headers.append("\r\n");

  const auto body = wire.subspan(header_end + 4);
  std::vector<std::byte> result(headers.size() + body.size());
  std::memcpy(result.data(), headers.data(), headers.size());
  if (!body.empty())
    std::memcpy(
        result.data() + headers.size(),
        body.data(), body.size());
  return {
      std::move(result), std::move(websocket_extensions),
      is_websocket_upgrade};
}

void inspect_http1_response(
    parsed_http_response &parsed,
    std::string_view offered_websocket_extensions,
    const ntl::net::inspection::content_decoder_registry &decoders,
    ntl::net::websocket::permessage_deflate_parameters
        &websocket_compression) {
  if (parsed.websocket_upgrade()) {
    const auto negotiated =
        ntl::net::websocket::parse_permessage_deflate_response(
            parsed.websocket_extensions);
    if (!negotiated)
      throw std::system_error(
          static_cast<int>(negotiated.status()),
          std::system_category(),
          "origin negotiated an unsupported WebSocket extension");
    websocket_compression = *negotiated;
    if (websocket_compression.enabled &&
        !ascii_contains_ci(
            offered_websocket_extensions, "permessage-deflate"))
      throw std::runtime_error(
          "origin selected permessage-deflate without a "
          "matching browser offer");
  }

  if (parsed.body_decoded)
    return;
  const auto encoded = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(parsed.body));
  auto decoded = ntl::net::inspection::decode_content_encoding(
      decoders, encoded, parsed.content_encoding,
      {.maximum_encoded_size = maximum_http_body_size,
       .maximum_decoded_size = maximum_http_body_size,
       .maximum_expansion_ratio = 64,
       .maximum_coding_layers = 4});
  if (!decoded)
    throw std::system_error(
        static_cast<int>(decoded.status()),
        std::system_category(),
        "HTTP content decoder rejected the response");
  parsed.body = std::move(*decoded);
  parsed.body_decoded = true;
}

} // namespace crtsys::wfp_sample::browser_https::http1_detail
