#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "http2_inspection.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ntl/net/http2/framing>
#include <ntl/net/http2/hpack>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/framed_stream>

#include "bidirectional_relay.hpp"
#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

class http2_request_sink final
    : public ntl::net::http2::inspection_sink {
public:
  ntl::status on_headers(
      std::uint32_t stream_id,
      std::span<const ntl::net::http2::header_field> fields,
      bool end_stream) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found != streams_.end()) {
        if (!end_stream)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (field.name.empty() || field.name.front() == ':')
            return STATUS_DATA_ERROR;
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
        }
        streams_.erase(found);
        return ntl::status::ok();
      }
      if (streams_.size() >= maximum_tracked_streams)
        return STATUS_QUOTA_EXCEEDED;

      bool regular_seen = false;
      bool method_seen = false;
      bool scheme_seen = false;
      bool authority_seen = false;
      bool path_seen = false;
      bool connect = false;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (field.name.empty())
          return STATUS_DATA_ERROR;
        for (const unsigned char character : field.name) {
          if (character >= 'A' && character <= 'Z')
            return STATUS_DATA_ERROR;
        }
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (!pseudo) {
          if (!valid_regular_header(field))
            return STATUS_DATA_ERROR;
          if (field.name == "content-length") {
            std::size_t parsed = 0;
            const auto converted = std::from_chars(
                field.value.data(),
                field.value.data() + field.value.size(),
                parsed);
            if (converted.ec != std::errc{} ||
                converted.ptr !=
                    field.value.data() + field.value.size() ||
                (content_length && *content_length != parsed))
              return STATUS_DATA_ERROR;
            content_length = parsed;
          }
          continue;
        }

        if (field.name == ":method" && !method_seen) {
          method_seen = true;
          connect = field.value == "CONNECT";
        } else if (field.name == ":scheme" && !scheme_seen) {
          scheme_seen = true;
        } else if (field.name == ":authority" &&
                   !authority_seen) {
          authority_seen = true;
        } else if (field.name == ":path" && !path_seen) {
          path_seen = true;
        } else {
          // Extended CONNECT (:protocol) is a separate product path.
          return STATUS_NOT_SUPPORTED;
        }
      }

      if (!method_seen || !authority_seen ||
          (connect ? (scheme_seen || path_seen)
                   : (!scheme_seen || !path_seen)))
        return STATUS_DATA_ERROR;
      if (end_stream) {
        if (content_length && *content_length != 0)
          return STATUS_DATA_ERROR;
        return ntl::status::ok();
      }
      streams_.emplace(
          stream_id,
          request_state{content_length, 0});
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint32_t stream_id, ntl::net::scatter_view data,
      bool end_stream) noexcept override {
    const auto found = streams_.find(stream_id);
    if (found == streams_.end())
      return STATUS_DATA_ERROR;
    auto &stream = found->second;
    if (data.size() > maximum_http_body_size - stream.received)
      return STATUS_BUFFER_OVERFLOW;
    stream.received += data.size();
    if (!end_stream)
      return ntl::status::ok();
    if (stream.content_length &&
        *stream.content_length != stream.received)
      return STATUS_DATA_ERROR;
    streams_.erase(found);
    return ntl::status::ok();
  }

private:
  struct request_state {
    std::optional<std::size_t> content_length;
    std::size_t received = 0;
  };

  static bool valid_regular_header(
      const ntl::net::http2::header_field &field) noexcept {
    if (field.name == "connection" ||
        field.name == "proxy-connection" ||
        field.name == "keep-alive" ||
        field.name == "transfer-encoding" ||
        field.name == "upgrade")
      return false;
    return field.name != "te" ||
           ascii_equal_ci(
               trim_http_ows(field.value), "trailers");
  }

  static constexpr std::size_t maximum_tracked_streams = 256;
  std::unordered_map<std::uint32_t, request_state> streams_;
};

class http2_response_sink final
    : public ntl::net::http2::inspection_sink {
public:
  http2_response_sink(
      std::wstring server_name,
      const ntl::net::inspection::content_decoder_registry &decoders,
      browser_html_logger &logger) noexcept
      : server_name_(std::move(server_name)),
        decoders_(&decoders), logger_(&logger) {}

  ntl::status on_headers(
      std::uint32_t stream_id,
      std::span<const ntl::net::http2::header_field> fields,
      bool end_stream) noexcept override {
    try {
      auto found = streams_.find(stream_id);
      if (found == streams_.end()) {
        if (streams_.size() >= maximum_tracked_streams)
          return STATUS_QUOTA_EXCEEDED;
        found = streams_.try_emplace(stream_id).first;
      }
      auto &stream = found->second;
      if (stream.final_headers) {
        if (!end_stream)
          return STATUS_DATA_ERROR;
        for (const auto &field : fields) {
          if (field.name.empty() || field.name.front() == ':')
            return STATUS_DATA_ERROR;
        }
        return complete(stream_id);
      }

      bool regular_seen = false;
      bool status_seen = false;
      unsigned status = 0;
      std::string content_type;
      std::string content_encoding;
      std::optional<std::size_t> content_length;
      for (const auto &field : fields) {
        if (field.name.empty())
          return STATUS_DATA_ERROR;
        for (const unsigned char character : field.name) {
          if (character >= 'A' && character <= 'Z')
            return STATUS_DATA_ERROR;
        }
        const bool pseudo = field.name.front() == ':';
        if (pseudo && regular_seen)
          return STATUS_DATA_ERROR;
        regular_seen = regular_seen || !pseudo;
        if (pseudo) {
          if (field.name != ":status" || status_seen ||
              field.value.size() != 3)
            return STATUS_DATA_ERROR;
          const auto converted = std::from_chars(
              field.value.data(),
              field.value.data() + field.value.size(),
              status);
          if (converted.ec != std::errc{} ||
              converted.ptr !=
                  field.value.data() + field.value.size() ||
              status < 100 || status > 999)
            return STATUS_DATA_ERROR;
          status_seen = true;
          continue;
        }
        if (field.name == "connection" ||
            field.name == "proxy-connection" ||
            field.name == "keep-alive" ||
            field.name == "transfer-encoding" ||
            field.name == "upgrade")
          return STATUS_DATA_ERROR;
        if (field.name == "content-type")
          content_type = field.value;
        else if (field.name == "content-encoding") {
          if (!content_encoding.empty())
            content_encoding.append(", ");
          content_encoding.append(field.value);
        } else if (field.name == "content-length") {
          std::size_t parsed = 0;
          const auto converted = std::from_chars(
              field.value.data(),
              field.value.data() + field.value.size(),
              parsed);
          if (converted.ec != std::errc{} ||
              converted.ptr !=
                  field.value.data() + field.value.size() ||
              (content_length && *content_length != parsed))
            return STATUS_DATA_ERROR;
          content_length = parsed;
        }
      }
      if (!status_seen)
        return STATUS_DATA_ERROR;
      if (status >= 100 && status < 200) {
        if (end_stream)
          return STATUS_DATA_ERROR;
        return ntl::status::ok();
      }

      stream.status = status;
      stream.content_type = std::move(content_type);
      stream.content_encoding = std::move(content_encoding);
      stream.content_length = content_length;
      stream.final_headers = true;
      return end_stream ? complete(stream_id)
                        : ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(
      std::uint32_t stream_id, ntl::net::scatter_view data,
      bool end_stream) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers)
        return STATUS_DATA_ERROR;
      auto &body = found->second.body;
      if (data.size() > maximum_http_body_size - body.size()) {
        logger_->record_error(
            "HTTP/2 encoded body limit host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " retained=" + std::to_string(body.size()) +
            " incoming=" + std::to_string(data.size()) +
            " maximum=" +
            std::to_string(maximum_http_body_size));
        return STATUS_BUFFER_OVERFLOW;
      }
      if (data.size() >
          maximum_buffered_bodies - buffered_body_bytes_) {
        logger_->record_error(
            "HTTP/2 connection body quota host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " retained=" +
            std::to_string(buffered_body_bytes_) +
            " incoming=" + std::to_string(data.size()) +
            " maximum=" +
            std::to_string(maximum_buffered_bodies));
        return STATUS_QUOTA_EXCEEDED;
      }
      const auto copied = data.for_each_chunk(
          [&body](std::span<const std::byte> chunk) noexcept {
            try {
              body.insert(
                  body.end(), chunk.begin(), chunk.end());
              return true;
            } catch (...) {
              return false;
            }
          });
      if (!copied.is_ok())
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered_body_bytes_ += data.size();
      return end_stream ? complete(stream_id)
                        : ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  unsigned last_status() const noexcept {
    return last_status_;
  }

  const std::optional<std::filesystem::path> &
  html_path() const noexcept {
    return html_path_;
  }

private:
  struct stream_state {
    unsigned status = 0;
    std::string content_type;
    std::string content_encoding;
    std::optional<std::size_t> content_length;
    std::vector<std::byte> body;
    bool final_headers = false;
  };

  ntl::status complete(std::uint32_t stream_id) noexcept {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() ||
          !found->second.final_headers)
        return STATUS_DATA_ERROR;
      auto &stream = found->second;
      if (stream.content_length &&
          *stream.content_length != stream.body.size())
        return STATUS_DATA_ERROR;
      const std::size_t buffered_size = stream.body.size();
      const auto encoded =
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(stream.body));
      auto decoded = ntl::net::inspection::decode_content_encoding(
          *decoders_, encoded, stream.content_encoding,
          {.maximum_encoded_size = maximum_http_body_size,
           .maximum_decoded_size = maximum_http_body_size,
           .maximum_expansion_ratio = 64,
           .maximum_coding_layers = 4});
      if (!decoded) {
        logger_->record_error(
            "HTTP/2 content decode failed host=" +
            narrow_dns_name(server_name_) +
            " stream=" + std::to_string(stream_id) +
            " encoding=" +
            (stream.content_encoding.empty()
                 ? std::string("identity")
                 : stream.content_encoding) +
            " status=" +
            std::to_string(static_cast<std::uint32_t>(
                static_cast<NTSTATUS>(decoded.status()))));
        return decoded.status();
      }

      parsed_http_response response;
      response.status = stream.status;
      response.content_type = std::move(stream.content_type);
      response.content_encoding =
          std::move(stream.content_encoding);
      response.body = std::move(*decoded);
      response.wire_size = stream.body.size();
      response.body_decoded = true;
      last_status_ = response.status;
      auto logged =
          logger_->record_response(server_name_, response);
      if (logged && !html_path_)
        html_path_ = std::move(logged);
      streams_.erase(found);
      buffered_body_bytes_ -= buffered_size;
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  std::wstring server_name_;
  const ntl::net::inspection::content_decoder_registry *decoders_;
  browser_html_logger *logger_;
  static constexpr std::size_t maximum_tracked_streams = 256;
  static constexpr std::size_t maximum_buffered_bodies =
      16 * 1024 * 1024;
  std::unordered_map<std::uint32_t, stream_state> streams_;
  std::size_t buffered_body_bytes_ = 0;
  unsigned last_status_ = 0;
  std::optional<std::filesystem::path> html_path_;
};

coroutine_task<std::size_t> relay_http2_frames(
    ntl::net::tls_stream &source,
    ntl::net::tls_stream &destination,
    ntl::net::http2::connection_inspector &inspector,
    ntl::net::http2::inspection_sink &sink,
    bool expect_client_preface,
    std::string_view direction) {
  constexpr std::array<std::byte, 24> client_preface{
      std::byte{0x50}, std::byte{0x52}, std::byte{0x49},
      std::byte{0x20}, std::byte{0x2a}, std::byte{0x20},
      std::byte{0x48}, std::byte{0x54}, std::byte{0x54},
      std::byte{0x50}, std::byte{0x2f}, std::byte{0x32},
      std::byte{0x2e}, std::byte{0x30}, std::byte{0x0d},
      std::byte{0x0a}, std::byte{0x0d}, std::byte{0x0a},
      std::byte{0x53}, std::byte{0x4d}, std::byte{0x0d},
      std::byte{0x0a}, std::byte{0x0d}, std::byte{0x0a}};
  std::size_t relayed = 0;
  if (expect_client_preface) {
    std::array<std::byte, client_preface.size()> received{};
    std::size_t offset = 0;
    while (offset != received.size()) {
      const std::size_t count = co_await source.read_some(
          std::span<std::byte>(received).subspan(offset));
      if (count == 0)
        throw std::runtime_error(
            "HTTP/2 client closed before its connection preface");
      offset += count;
    }
    if (received != client_preface)
      throw std::runtime_error(
          "HTTP/2 client sent an invalid connection preface");
    if (co_await destination.write_all(received) !=
        received.size())
      throw std::runtime_error(
          "HTTP/2 preface relay completed short");
    relayed += received.size();
  }

  constexpr std::size_t maximum_frame_payload =
      1024 * 1024;
  ntl::net::tls_framed_stream frames(
      source,
      ntl::net::http2::frame_framer(
          {maximum_frame_payload, false}),
      {maximum_frame_payload + ntl::net::http2::frame_header_size},
      16 * 1024);
  for (;;) {
    auto message = co_await frames.read_frame_or_eof();
    if (!message)
      co_return relayed;
    const auto wire = ntl::net::scatter_view::from_contiguous(
        message->frame());
    const auto frame = ntl::net::http2::frame_view::parse(
        wire, {maximum_frame_payload, false});
    if (!frame)
      throw std::runtime_error(
          "HTTP/2 frame failed validation after framing");
    const auto inspected = inspector.consume(*frame, sink);
    if (!inspected.is_ok()) {
      const auto native_status = static_cast<std::uint32_t>(
          static_cast<NTSTATUS>(inspected));
      std::array<char, 9> status_text{};
      const auto converted = std::to_chars(
          status_text.data(),
          status_text.data() + status_text.size() - 1,
          native_status, 16);
      const std::string status_hex(
          status_text.data(), converted.ptr);
      throw std::runtime_error(
          "HTTP/2 inspection rejected frame direction=" +
          std::string(direction) + " type=" +
          std::to_string(static_cast<unsigned>(
              frame->header().type)) +
          " flags=" +
          std::to_string(frame->header().flags) +
          " stream=" +
          std::to_string(frame->header().stream_id) +
          " status=0x" + status_hex);
    }
    if (co_await destination.write_all(message->frame()) !=
        message->size())
      throw std::runtime_error(
          "HTTP/2 frame relay completed short");
    relayed += message->size();
  }
}

} // namespace

nested_task<browser_proxy_result> relay_http2_connection(
    SOCKET inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound,
    std::wstring server_name,
    const ntl::net::inspection::content_decoder_registry &decoders,
    browser_html_logger &logger) {
  ntl::net::http2::hpack_decoder_adapter<
      ntl::net::http2::bounded_hpack_decoder>
      request_decoder(ntl::net::http2::hpack_limits{64 * 1024});
  ntl::net::http2::hpack_decoder_adapter<
      ntl::net::http2::bounded_hpack_decoder>
      response_decoder(ntl::net::http2::hpack_limits{64 * 1024});
  ntl::net::http2::connection_inspector request_inspector(
      request_decoder, 256 * 1024, 256 * 1024);
  ntl::net::http2::connection_inspector response_inspector(
      response_decoder, 256 * 1024, 256 * 1024);
  http2_request_sink request_sink;
  http2_response_sink response_sink(
      server_name, decoders, logger);
  auto client_to_origin = relay_http2_frames(
      inbound, outbound, request_inspector,
      request_sink, true, "browser-to-origin");
  auto origin_to_client = relay_http2_frames(
      outbound, inbound, response_inspector,
      response_sink, false, "origin-to-browser");
  co_await join_bidirectional_relays(
      std::move(client_to_origin),
      std::move(origin_to_client),
      inbound_socket, outbound_socket);
  co_return browser_proxy_result{
      std::move(server_name), response_sink.last_status(),
      response_sink.html_path()};
}

} // namespace crtsys::wfp_sample::browser_https
