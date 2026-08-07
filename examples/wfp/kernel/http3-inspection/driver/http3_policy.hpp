#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/http3/framing>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/inspection/content_encoder>
#include <ntl/result>
#include <ntl/status>

#include "http3_inspection_policy.hpp"

namespace crtsys::wfp_kernel_http3 {

inline constexpr std::string_view application_protocol = "h3";
inline constexpr std::size_t maximum_request_body_size = 32 * 1024;
inline constexpr std::size_t maximum_header_block_size = 16 * 1024;
inline constexpr std::size_t maximum_response_body_size = 64 * 1024;
inline constexpr std::size_t maximum_capsule_payload_size = 4096;
inline constexpr std::size_t maximum_buffered_capsule_bytes = 64 * 1024;

inline bool supports_application_protocol(std::string_view value) noexcept {
  return value == application_protocol;
}

inline constexpr ntl::net::http3::webtransport::prerequisites
required_webtransport_features() noexcept {
  return {.peer_enabled_extended_connect = true,
          .peer_enabled_webtransport = true,
          .local_h3_datagram = true,
          .peer_h3_datagram = true,
          .local_quic_datagram = true,
          .peer_quic_datagram = true,
          .local_reset_stream_at = true,
          .peer_reset_stream_at = true};
}

struct request_policy {
  std::string path;
  std::string content_encoding;
  bool blocked = false;
  bool webtransport = false;
};

inline ntl::result<request_policy> classify_request(
    std::span<const ntl::net::http3::header_field> fields,
    ntl::net::http3::webtransport::prerequisites webtransport_features =
        required_webtransport_features()) noexcept {
  try {
    request_policy result;
    std::string_view method;
    std::string_view protocol;
    for (const auto &field : fields) {
      if (field.name == ":method")
        method = field.value;
      else if (field.name == ":protocol")
        protocol = field.value;
      else if (field.name == ":path")
        result.path = field.value;
    }
    if (method == "CONNECT" &&
        protocol == ntl::net::http3::webtransport::upgrade_token) {
      auto validated =
          ntl::net::http3::webtransport::validate_session_request(
              fields, webtransport_features);
      if (!validated)
        return ntl::unexpected(validated.status());
      result.path = std::move(validated->path);
      result.webtransport = true;
      result.blocked =
          crtsys::examples::wfp::http3_inspection::block_requested(fields);
      return ntl::ok(std::move(result));
    }
    if (method != "GET" && method != "POST")
      return ntl::unexpected(STATUS_DATA_ERROR);
    result.blocked =
        crtsys::examples::wfp::http3_inspection::ordinary_request_blocked(
            fields);
    if (result.path == "/gzip")
      result.content_encoding = "gzip";
    else if (result.path == "/deflate")
      result.content_encoding = "deflate";
    else if (result.path == "/br")
      result.content_encoding = "br";
    return ntl::ok(std::move(result));
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

inline ntl::net::http3::webtransport::transform_session
make_webtransport_policy() {
  ntl::net::http3::webtransport::transform_session result({
      .session = {.maximum_bidirectional_streams = 8,
                  .maximum_unidirectional_streams = 8,
                  .maximum_stream_data = 64 * 1024,
                  .maximum_datagram_payload = 4096,
                  .maximum_datagrams = 32},
      .maximum_rewritten_payload = 4096,
      .maximum_expansion_ratio = 4});
  crtsys::examples::wfp::http3_inspection::configure_webtransport_policy(
      result);
  return result;
}

class bounded_capsule_stream {
public:
  template <class Callback>
  ntl::status consume(ntl::net::scatter_view input,
                      Callback &&callback) noexcept {
    try {
      if (input) {
        if (input.size() >
            maximum_buffered_capsule_bytes - buffer_.size())
          return STATUS_BUFFER_OVERFLOW;
        const std::size_t offset = buffer_.size();
        buffer_.resize(offset + input.size());
        if (!input.copy_to(
                     std::span<std::byte>(buffer_).subspan(offset))
                 .is_ok()) {
          buffer_.resize(offset);
          return STATUS_DATA_ERROR;
        }
      }

      ntl::net::http::capsule_framer framer(
          {.maximum_payload_size = maximum_capsule_payload_size});
      while (!buffer_.empty()) {
        const auto buffered = ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(buffer_));
        const auto probe = framer.probe(buffered);
        if (probe.state() == ntl::net::framing::probe_state::need_more)
          return ntl::status::ok();
        if (probe.state() != ntl::net::framing::probe_state::complete)
          return probe.error();
        auto wire = buffered.subview(0, probe.frame_size());
        if (!wire)
          return wire.status();
        auto capsule = ntl::net::http::capsule_view::parse(
            *wire, {.maximum_payload_size = maximum_capsule_payload_size});
        if (!capsule)
          return capsule.status();
        const ntl::status inspected = callback(*capsule);
        if (!inspected.is_ok())
          return inspected;
        buffer_.erase(
            buffer_.begin(),
            buffer_.begin() +
                static_cast<std::ptrdiff_t>(probe.frame_size()));
      }
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  ntl::status finish() const noexcept {
    return buffer_.empty() ? ntl::status::ok()
                           : ntl::status{STATUS_END_OF_FILE};
  }

  std::size_t buffered_size() const noexcept { return buffer_.size(); }

private:
  std::vector<std::byte> buffer_;
};

struct response_policy {
  std::vector<ntl::net::http3::header_field> headers;
  std::vector<std::byte> semantic_body;
  std::vector<std::byte> encoded_body;
  std::vector<std::byte> wire;
};

inline ntl::status append_frame(
    std::vector<std::byte> &output, ntl::net::http3::frame_type type,
    std::span<const std::byte> payload) noexcept {
  auto status = ntl::net::http3::append_quic_varint(
      output, static_cast<std::uint64_t>(type));
  if (status.is_ok())
    status = ntl::net::http3::append_quic_varint(output, payload.size());
  if (!status.is_ok())
    return status;
  try {
    output.insert(output.end(), payload.begin(), payload.end());
    return ntl::status::ok();
  } catch (const std::bad_alloc &) {
    return STATUS_INSUFFICIENT_RESOURCES;
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }
}

inline ntl::result<response_policy> build_response(
    bool blocked, std::string_view content_encoding,
    const ntl::net::inspection::content_encoder_registry &encoders) noexcept {
  try {
    const std::string_view body =
        blocked ? crtsys::examples::wfp::http3_inspection::blocked_html
                : crtsys::examples::wfp::http3_inspection::allowed_html;

    response_policy result;
    result.semantic_body.assign(
        reinterpret_cast<const std::byte *>(body.data()),
        reinterpret_cast<const std::byte *>(body.data() + body.size()));
    result.encoded_body = result.semantic_body;
    if (!content_encoding.empty()) {
      auto encoded = ntl::net::inspection::encode_content_encoding(
          encoders, result.semantic_body, content_encoding,
          {.maximum_input_size = maximum_response_body_size,
           .maximum_encoded_size = maximum_response_body_size,
           .maximum_coding_layers = 1});
      if (!encoded)
        return ntl::unexpected(encoded.status());
      result.encoded_body = std::move(*encoded);
    }

    result.headers = {
        {":status", blocked ? "403" : "200"},
        {"content-type", "text/html; charset=utf-8"},
        {"content-length", std::to_string(result.encoded_body.size())}};
    if (!content_encoding.empty())
      result.headers.push_back(
          {"content-encoding", std::string(content_encoding)});

    ntl::net::http3::bounded_static_qpack_encoder encoder;
    auto header_block =
        encoder.encode(result.headers, maximum_header_block_size);
    if (!header_block)
      return ntl::unexpected(header_block.status());
    result.wire.reserve(header_block->size() + result.encoded_body.size() +
                        16);
    auto status = append_frame(result.wire,
                               ntl::net::http3::frame_type::headers,
                               *header_block);
    if (status.is_ok())
      status = append_frame(result.wire,
                            ntl::net::http3::frame_type::data,
                            result.encoded_body);
    if (!status.is_ok())
      return ntl::unexpected(static_cast<NTSTATUS>(status));
    return ntl::ok(std::move(result));
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

} // namespace crtsys::wfp_kernel_http3
