#pragma once

#include <ntddk.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ntl/net/buffer/scatter_view>
#include <ntl/net/http/http1_framing>
#include <ntl/net/http/http1_transform>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/transform>
#include <ntl/net/grpc/transform>
#include <ntl/net/http2/flow_control>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/io/async_transport_stream>
#include <ntl/net/kernel/content_codecs>
#include <ntl/net/kernel/schannel>
#include <ntl/net/kernel/tls_client_hello>
#include <ntl/net/kernel/tls_stream>
#include <ntl/net/kernel/waitable_task>
#include <ntl/net/kernel/wsk_redirect>
#include <ntl/net/websocket/stream_transform>
#include <ntl/net/websocket/transform>

#include "browser_https_inspection_contract.hpp"
#include "browser_http_policy.hpp"
#include "request_capture.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

namespace contract = wfp_kernel_browser_https_inspection;

inline ntl::net::read_options long_lived_read_options() noexcept {
  return {.timeout = (std::chrono::milliseconds::max)()};
}

inline bool ascii_server_name_equal(std::string_view left,
                                    std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    const auto lower = [](unsigned char value) noexcept {
      return value >= 'A' && value <= 'Z'
                 ? static_cast<unsigned char>(value + ('a' - 'A'))
                 : value;
    };
    if (lower(static_cast<unsigned char>(left[index])) !=
        lower(static_cast<unsigned char>(right[index])))
      return false;
  }
  return true;
}

struct tcp_session_observer {
  // Retains the callback target for the complete proxy operation.
  std::shared_ptr<void> owner;
  void *context = nullptr;
  void (*publish)(void *, std::uint64_t,
                  const ntl::net::kernel::ip_endpoint &, std::string_view,
                  contract::inspected_protocol, contract::inspection_action,
                  std::uint32_t, NTSTATUS, std::uint32_t,
                  std::span<const std::byte>,
                  std::span<const std::byte>) noexcept = nullptr;
  std::atomic<std::uint64_t> *permitted = nullptr;
  std::atomic<std::uint64_t> *blocked = nullptr;
  std::atomic<std::uint64_t> *transformed = nullptr;
  std::atomic<std::uint64_t> *origin_completed = nullptr;
  std::uint64_t session_id = 0;
  ntl::net::kernel::ip_endpoint original_destination{};
};

inline bool has_html_content_type(
    const ntl::net::http::header_collection &headers) noexcept {
  const auto value = headers.first("content-type");
  return value && ntl::net::http::transform_detail::html_content_type(*value);
}

class buffered_tls_reader {
public:
  buffered_tls_reader() = default;
  buffered_tls_reader(const buffered_tls_reader &) = delete;
  buffered_tls_reader &operator=(const buffered_tls_reader &) = delete;

  ntl::status append_buffered(std::span<const std::byte> bytes) noexcept {
    try {
      if (bytes.size() > (4 * 1024 * 1024 + 64 * 1024) - buffered_size())
        return STATUS_BUFFER_OVERFLOW;
      compact();
      storage_.insert(storage_.end(), bytes.begin(), bytes.end());
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  ntl::net::kernel::task<ntl::result<std::size_t>>
  read_some_borrowed(ntl::net::kernel::tls_stream &stream,
                     std::span<std::byte> destination) {
    if (destination.empty())
      co_return ntl::ok<std::size_t>(0);
    const std::size_t available = buffered_size();
    if (available != 0) {
      const std::size_t count =
          (std::min)(available, destination.size());
      std::memcpy(destination.data(), storage_.data() + offset_, count);
      consume(count);
      co_return ntl::ok(count);
    }
    co_return co_await stream.read_some_borrowed(destination,
                                        long_lived_read_options());
  }

  ntl::net::kernel::task<ntl::result<std::vector<std::byte>>>
  read_http1(ntl::net::kernel::tls_stream &stream,
             ntl::net::http::http1_message_kind kind,
             bool response_body_forbidden = false,
             bool allow_close_delimited_response = false) {
    try {
      constexpr std::size_t maximum = 4 * 1024 * 1024 + 64 * 1024;
      constexpr std::size_t receive_chunk = 16 * 1024;
      const ntl::net::http::http1_message_framer framer(
          kind,
          {.maximum_header_size = 64 * 1024,
           .maximum_body_size = 4 * 1024 * 1024,
           .maximum_chunk_line_size = 4 * 1024,
           .maximum_trailer_size = 16 * 1024,
           .allow_close_delimited_response =
               allow_close_delimited_response,
           .response_body_forbidden = response_body_forbidden});
      for (;;) {
        const auto bytes = std::span<const std::byte>(storage_).subspan(offset_);
        if (!bytes.empty()) {
          const auto view =
              ntl::net::scatter_view::from_contiguous(bytes);
          const auto probe = ntl::net::framing::validate(
              eof_ ? framer.finish(view) : framer.probe(view), bytes.size(),
              ntl::net::framing::frame_limits{maximum});
          if (probe.state() == ntl::net::framing::probe_state::malformed)
            co_return ntl::unexpected(probe.error());
          if (probe.state() == ntl::net::framing::probe_state::complete) {
            std::vector<std::byte> result(
                bytes.begin(),
                bytes.begin() +
                    static_cast<std::ptrdiff_t>(probe.frame_size()));
            consume(probe.frame_size());
            co_return ntl::ok(std::move(result));
          }
        }
        if (eof_)
          co_return ntl::unexpected(STATUS_END_OF_FILE);
        if (buffered_size() >= maximum)
          co_return ntl::unexpected(STATUS_BUFFER_OVERFLOW);
        compact();
        std::array<std::byte, receive_chunk> chunk{};
        auto received = co_await stream.read_some_borrowed(
            chunk, long_lived_read_options());
        if (!received)
          co_return ntl::unexpected(received.status());
        if (*received == 0) {
          eof_ = true;
          continue;
        }
        if (*received > maximum - storage_.size())
          co_return ntl::unexpected(STATUS_BUFFER_OVERFLOW);
        storage_.insert(storage_.end(), chunk.begin(),
                        chunk.begin() +
                            static_cast<std::ptrdiff_t>(*received));
      }
    } catch (const std::bad_alloc &) {
      co_return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      co_return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  bool eof() const noexcept { return eof_; }
  std::size_t buffered_size() const noexcept {
    return storage_.size() - offset_;
  }

private:
  void consume(std::size_t count) noexcept {
    NT_ASSERT(count <= buffered_size());
    offset_ += count;
    if (offset_ == storage_.size()) {
      storage_.clear();
      offset_ = 0;
    }
  }

  void compact() {
    if (offset_ == 0)
      return;
    storage_.erase(storage_.begin(),
                   storage_.begin() +
                       static_cast<std::ptrdiff_t>(offset_));
    offset_ = 0;
  }

  std::vector<std::byte> storage_;
  std::size_t offset_ = 0;
  bool eof_ = false;
};

inline ntl::net::kernel::task<ntl::status>
read_exactly(ntl::net::kernel::tls_stream &stream,
             std::span<std::byte> destination) {
  std::size_t used = 0;
  while (used != destination.size()) {
    auto received = co_await stream.read_some_borrowed(
        destination.subspan(used), long_lived_read_options());
    if (!received)
      co_return received.status();
    if (*received == 0)
      co_return ntl::status{STATUS_END_OF_FILE};
    used += *received;
  }
  co_return ntl::status::ok();
}

inline ntl::net::kernel::task<ntl::status>
read_exactly(buffered_tls_reader &reader,
             ntl::net::kernel::tls_stream &stream,
             std::span<std::byte> destination) {
  std::size_t used = 0;
  while (used != destination.size()) {
    auto received = co_await reader.read_some_borrowed(
        stream, destination.subspan(used));
    if (!received)
      co_return received.status();
    if (*received == 0)
      co_return ntl::status{STATUS_END_OF_FILE};
    used += *received;
  }
  co_return ntl::status::ok();
}

inline ntl::net::kernel::task<ntl::status>
write_all(ntl::net::kernel::tls_stream &stream,
          std::span<const std::byte> bytes) {
  auto written = co_await stream.write_all(bytes);
  if (!written)
    co_return written.status();
  co_return *written == bytes.size() ? ntl::status::ok()
                                     : ntl::status{STATUS_DATA_ERROR};
}

inline bool has_content_encoding(
    const ntl::net::http::header_collection &headers) noexcept {
  const auto value = headers.first("content-encoding");
  return value && !value->empty() && *value != "identity";
}

inline bool has_grpc_content_type(
    const ntl::net::http::header_collection &headers) noexcept {
  const auto value = headers.first("content-type");
  return value &&
         crtsys::wfp_browser_http_policy::grpc_content_type(*value);
}


} // namespace crtsys::wfp_kernel_browser_https::driver
