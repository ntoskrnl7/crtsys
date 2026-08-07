#pragma once

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include <ntl/net/http/transform>
#include <ntl/result>
#include <ntl/status>

namespace crtsys::wfp_kernel_browser_https::driver {

struct request_capture_limits {
  std::size_t maximum_size = 4096;
  std::size_t maximum_method_size = 32;
};

inline ntl::result<std::string> sanitize_request_capture(
    const ntl::net::http::request_message &request,
    request_capture_limits limits = {}) noexcept {
  if (limits.maximum_size == 0 || limits.maximum_method_size == 0 ||
      limits.maximum_method_size > limits.maximum_size)
    return ntl::unexpected(STATUS_INVALID_PARAMETER);

  try {
    std::string result;
    result.reserve((std::min)(limits.maximum_size, std::size_t{256}));

    const auto append = [&](std::string_view value) noexcept {
      if (value.size() > limits.maximum_size - result.size())
        return false;
      try {
        result.append(value);
        return true;
      } catch (...) {
        return false;
      }
    };
    const auto append_text = [&](std::string_view value,
                                 std::size_t maximum) noexcept {
      const std::size_t available = limits.maximum_size - result.size();
      const std::size_t count =
          (std::min)((std::min)(value.size(), maximum), available);
      try {
        for (std::size_t index = 0; index != count; ++index) {
          const unsigned char character =
              static_cast<unsigned char>(value[index]);
          result.push_back(character >= 0x20 && character <= 0x7e
                               ? static_cast<char>(character)
                               : '?');
        }
        // The method is deliberately truncated to its configured bound.
        // Truncation is a successful sanitization, not an error that should
        // make an otherwise valid request fail closed.
        return true;
      } catch (...) {
        return false;
      }
    };
    const auto append_decimal = [&](std::size_t value) noexcept {
      char digits[32]{};
      std::size_t count = 0;
      do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
      } while (value != 0);
      if (count > limits.maximum_size - result.size())
        return false;
      try {
        while (count != 0)
          result.push_back(digits[--count]);
        return true;
      } catch (...) {
        return false;
      }
    };
    const auto protocol_name = [&]() noexcept -> std::string_view {
      switch (request.wire_protocol) {
      case ntl::net::http::protocol::http1:
        return "http/1.1";
      case ntl::net::http::protocol::http2:
        return "h2";
      case ntl::net::http::protocol::http3:
        return "h3";
      }
      return "unknown";
    };

    if (!append("method: ") ||
        !append_text(request.method, limits.maximum_method_size) ||
        !append("\nprotocol: ") || !append(protocol_name()) ||
        !append("\nheader-count: ") ||
        !append_decimal(request.headers.size()) ||
        !append("\nbody-bytes: ") || !append_decimal(request.body.size()) ||
        !append("\ntunnel: ") ||
        !append(request.extended_protocol ? "yes" : "no") ||
        !append("\ncontent-coded: ") ||
        !append(request.headers.first("content-encoding") ? "yes" : "no"))
      return ntl::unexpected(STATUS_BUFFER_OVERFLOW);
    return ntl::ok(std::move(result));
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

inline std::string capture_request(
    const ntl::net::http::request_message &request) {
  auto captured = sanitize_request_capture(request);
  if (!captured)
    throw std::bad_alloc();
  return std::move(*captured);
}

} // namespace crtsys::wfp_kernel_browser_https::driver
