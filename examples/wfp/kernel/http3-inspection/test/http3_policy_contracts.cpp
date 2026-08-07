#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/http/datagram>
#include <ntl/net/http3/backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

#include "http3_policy.hpp"

namespace {

std::vector<std::byte> bytes(std::string_view value) {
  std::vector<std::byte> result(value.size());
  if (!value.empty())
    std::memcpy(result.data(), value.data(), value.size());
  return result;
}

std::string text(std::span<const std::byte> value) {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

std::string text(ntl::net::scatter_view value) {
  std::vector<std::byte> copied(value.size());
  if (!copied.empty() && !value.copy_to(copied).is_ok())
    return {};
  return text(copied);
}

bool has_header(std::span<const ntl::net::http3::header_field> fields,
                std::string_view name, std::string_view value) {
  for (const auto &field : fields) {
    if (field.name == name && field.value == value)
      return true;
  }
  return false;
}

std::vector<ntl::net::http3::header_field>
ordinary_request(std::string path, bool blocked = false) {
  std::vector<ntl::net::http3::header_field> fields{
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "localhost"},
      {":path", std::move(path)}};
  if (blocked)
    fields.push_back({"x-ntl-block", "1"});
  return fields;
}

bool request_policy_contract() {
  constexpr std::array<std::pair<std::string_view, std::string_view>, 4>
      cases{{{"/", ""},
             {"/gzip", "gzip"},
             {"/deflate", "deflate"},
             {"/br", "br"}}};
  for (const auto &[path, encoding] : cases) {
    const auto fields = ordinary_request(std::string(path));
    auto result = crtsys::wfp_kernel_http3::classify_request(fields);
    if (!result || result->path != path ||
        result->content_encoding != encoding || result->blocked ||
        result->webtransport)
      return false;
  }

  auto blocked_fields = ordinary_request("/blocked", true);
  auto blocked =
      crtsys::wfp_kernel_http3::classify_request(blocked_fields);
  if (!blocked || !blocked->blocked)
    return false;

  auto invalid = ordinary_request("/");
  invalid[0].value = "DELETE";
  if (crtsys::wfp_kernel_http3::classify_request(invalid))
    return false;

  const std::vector<ntl::net::http3::header_field> webtransport{
      {":method", "CONNECT"},
      {":protocol", "webtransport-h3"},
      {":scheme", "https"},
      {":authority", "localhost"},
      {":path", "/session"},
      {"origin", "https://localhost"},
      {"capsule-protocol", "?1"}};
  auto accepted =
      crtsys::wfp_kernel_http3::classify_request(webtransport);
  if (!accepted || !accepted->webtransport ||
      accepted->path != "/session")
    return false;
  auto blocked_webtransport = webtransport;
  blocked_webtransport.push_back({"x-ntl-block", "1"});
  blocked_webtransport.push_back({"x-ntl-block", "0"});
  auto rejected =
      crtsys::wfp_kernel_http3::classify_request(blocked_webtransport);
  if (!rejected || !rejected->webtransport || !rejected->blocked)
    return false;
  return !crtsys::wfp_kernel_http3::classify_request(
      webtransport, ntl::net::http3::webtransport::prerequisites{});
}

class policy_sink final : public ntl::net::http3::inspection_sink {
public:
  ntl::status on_headers(
      std::uint64_t,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    auto classified = crtsys::wfp_kernel_http3::classify_request(fields);
    if (!classified)
      return classified.status();
    request = std::move(*classified);
    headers_seen = true;
    return ntl::status::ok();
  }

  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }

  ntl::status on_stream_end(std::uint64_t) noexcept override {
    ended = true;
    return ntl::status::ok();
  }

  crtsys::wfp_kernel_http3::request_policy request;
  bool headers_seen = false;
  bool ended = false;
};

bool dynamic_qpack_contract() {
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      qpack(ntl::net::http3::dynamic_qpack_limits{
          .maximum_table_capacity = 256,
          .maximum_blocked_streams = 2,
          .maximum_encoder_stream_buffer = 4096,
          .maximum_literal_size = 16 * 1024});
  ntl::net::http3::borrowed_connection_inspector inspector(
      qpack,
      {.maximum_concurrent_request_streams = 2,
       .maximum_buffered_bytes_per_stream = 64 * 1024,
       .frames = {64 * 1024}},
      16 * 1024);
  policy_sink sink;

  auto fields = ordinary_request("/gzip");
  ntl::net::http3::bounded_static_qpack_encoder encoder;
  auto header_block = encoder.encode(fields, 16 * 1024);
  if (!header_block || header_block->size() < 2) {
    std::cerr << "dynamic qpack: static encode failed\n";
    return false;
  }
  (*header_block)[0] = std::byte{0x02};
  (*header_block)[1] = std::byte{0x00};
  header_block->push_back(std::byte{0x80});
  std::vector<std::byte> wire;
  if (!crtsys::wfp_kernel_http3::append_frame(
           wire, ntl::net::http3::frame_type::headers, *header_block)
           .is_ok()) {
    std::cerr << "dynamic qpack: frame encode failed\n";
    return false;
  }

  ntl::status blocked = ntl::status::ok();
  constexpr std::array<std::size_t, 4> chunks{1, 2, 3, 5};
  for (std::size_t offset = 0, index = 0; offset != wire.size(); ++index) {
    const std::size_t count =
        (std::min)(chunks[index % chunks.size()], wire.size() - offset);
    const bool final = offset + count == wire.size();
    blocked = inspector.consume_request_stream(
        0,
        ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(wire).subspan(offset, count)),
        final, sink);
    if (!final && !blocked.is_ok()) {
      std::cerr << "dynamic qpack: fragmented frame failed early\n";
      return false;
    }
    offset += count;
  }
  if (blocked != STATUS_RETRY || sink.headers_seen || sink.ended) {
    std::cerr << "dynamic qpack: expected retry, status="
              << static_cast<long>(static_cast<NTSTATUS>(blocked)) << '\n';
    return false;
  }

  // The MsQuic backend strips the unidirectional stream type (0x02)
  // before delivering these encoder instructions to borrowed_connection_inspector.
  constexpr std::array<std::byte, 6> instructions{
      std::byte{0x3f}, std::byte{0x21}, std::byte{0x41},
      std::byte{0x78}, std::byte{0x01}, std::byte{0x79}};
  const auto consumed = inspector.consume_qpack_encoder_stream(
      ntl::net::scatter_view::from_contiguous(instructions));
  const auto resumed = consumed.is_ok()
                           ? inspector.resume_request_stream(0, sink)
                           : consumed;
  if (!consumed.is_ok() || !resumed.is_ok() || !sink.headers_seen ||
      !sink.ended || sink.request.content_encoding != "gzip") {
    std::cerr << "dynamic qpack: consume="
              << static_cast<long>(static_cast<NTSTATUS>(consumed))
              << " resume="
              << static_cast<long>(static_cast<NTSTATUS>(resumed))
              << " headers=" << sink.headers_seen << " ended=" << sink.ended
              << " encoding=" << sink.request.content_encoding << '\n';
    return false;
  }
  auto acknowledgment = inspector.take_qpack_decoder_stream();
  if (!acknowledgment || acknowledgment->empty()) {
    std::cerr << "dynamic qpack: decoder acknowledgment missing\n";
    return false;
  }
  return true;
}

bool response_contract() {
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_encoders(encoders);
  ntl::net::inspection::register_standard_content_decoders(decoders);

  for (const std::string_view encoding : {"gzip", "deflate", "br"}) {
    auto response = crtsys::wfp_kernel_http3::build_response(
        false, encoding, encoders);
    if (!response || response->wire.empty() ||
        !has_header(response->headers, ":status", "200") ||
        !has_header(response->headers, "content-encoding", encoding))
      return false;

    const auto wire =
        ntl::net::scatter_view::from_contiguous(response->wire);
    auto headers_frame = ntl::net::http3::frame_view::parse(
        wire, {crtsys::wfp_kernel_http3::maximum_response_body_size});
    if (!headers_frame || headers_frame->header().type() !=
                              ntl::net::http3::frame_type::headers)
      return false;
    ntl::net::http3::bounded_static_qpack_decoder decoder;
    auto decoded_headers = decoder.decode(
        0, headers_frame->payload(),
        crtsys::wfp_kernel_http3::maximum_header_block_size);
    if (!decoded_headers ||
        !has_header(decoded_headers->fields, "content-encoding", encoding))
      return false;

    const std::size_t first_size =
        headers_frame->header().header_size +
        static_cast<std::size_t>(headers_frame->header().payload_size);
    auto data_wire = wire.subview(first_size, wire.size() - first_size);
    if (!data_wire)
      return false;
    auto data_frame = ntl::net::http3::frame_view::parse(
        *data_wire, {crtsys::wfp_kernel_http3::maximum_response_body_size});
    if (!data_frame || data_frame->header().type() !=
                           ntl::net::http3::frame_type::data)
      return false;
    auto decoded = ntl::net::inspection::decode_content_encoding(
        decoders, data_frame->payload(), encoding,
        {.maximum_encoded_size =
             crtsys::wfp_kernel_http3::maximum_response_body_size,
         .maximum_decoded_size =
             crtsys::wfp_kernel_http3::maximum_response_body_size,
         .maximum_expansion_ratio = 16,
         .maximum_coding_layers = 1});
    if (!decoded || *decoded != response->semantic_body)
      return false;
  }

  auto denied =
      crtsys::wfp_kernel_http3::build_response(true, "", encoders);
  if (!denied || !has_header(denied->headers, ":status", "403") ||
      text(denied->semantic_body).find("blocked by NTL HTTP/3 policy") ==
          std::string::npos)
    return false;
  return !crtsys::wfp_kernel_http3::build_response(
      false, "unsupported-coding", encoders);
}

bool webtransport_contract() {
  auto policy = crtsys::wfp_kernel_http3::make_webtransport_policy();
  if (!policy
           .open_stream(ntl::net::http3::webtransport::stream_direction::
                            bidirectional)
           .is_ok())
    return false;
  ntl::net::http3::webtransport::payload payload{
      .kind = ntl::net::http3::webtransport::payload_kind::stream,
      .session_id = 0,
      .direction =
          ntl::net::http3::webtransport::stream_direction::bidirectional,
      .bytes = bytes("client-payload")};
  const auto transformed = policy.apply(payload);
  if (transformed.action !=
          ntl::net::http3::webtransport::transform_action::forward ||
      transformed.failure != STATUS_SUCCESS || !transformed.modified ||
      text(payload.bytes) != "ntl-inspected-payload")
    return false;

  auto bounded = crtsys::wfp_kernel_http3::make_webtransport_policy();
  ntl::net::http3::webtransport::payload oversized{
      .kind = ntl::net::http3::webtransport::payload_kind::datagram,
      .session_id = 0,
      .bytes = std::vector<std::byte>(4097)};
  if (bounded.apply(oversized).failure != STATUS_BUFFER_OVERFLOW)
    return false;

  std::vector<std::byte> stream_wire;
  if (!ntl::net::http3::append_quic_varint(
           stream_wire,
           ntl::net::http3::webtransport::bidirectional_stream_signal)
           .is_ok() ||
      !ntl::net::http3::append_quic_varint(stream_wire, 0).is_ok())
    return false;
  const auto stream_body = bytes("client-payload");
  stream_wire.insert(stream_wire.end(), stream_body.begin(),
                     stream_body.end());
  auto prefix = ntl::net::http3::webtransport::parse_stream_prefix(
      ntl::net::http3::webtransport::stream_direction::bidirectional,
      ntl::net::scatter_view::from_contiguous(stream_wire));
  if (!prefix || prefix->session_id != 0 ||
      text(prefix->body) != "client-payload")
    return false;

  constexpr std::array<std::byte, 1> capsule_value{std::byte{1}};
  auto capsule_wire = ntl::net::http::encode_capsule(
      ntl::net::http3::webtransport::wt_max_data,
      capsule_value, {.maximum_payload_size = 4096});
  if (!capsule_wire)
    return false;
  auto capsule = ntl::net::http::capsule_view::parse(
      ntl::net::scatter_view::from_contiguous(*capsule_wire),
      {.maximum_payload_size = 4096});
  if (!capsule ||
      !ntl::net::http3::webtransport::inspect_capsule(*capsule))
    return false;

  crtsys::wfp_kernel_http3::bounded_capsule_stream reassembler;
  std::size_t inspected_count = 0;
  const auto inspect = [&](const ntl::net::http::capsule_view &value) {
    const auto inspected =
        ntl::net::http3::webtransport::inspect_capsule(value);
    if (inspected)
      ++inspected_count;
    return inspected ? ntl::status::ok() : inspected.status();
  };
  for (std::size_t offset = 0; offset != capsule_wire->size();) {
    const std::size_t count =
        (std::min)(std::size_t{2}, capsule_wire->size() - offset);
    if (!reassembler
             .consume(ntl::net::scatter_view::from_contiguous(
                          std::span<const std::byte>(*capsule_wire)
                              .subspan(offset, count)),
                      inspect)
             .is_ok())
      return false;
    offset += count;
  }
  if (!reassembler.finish().is_ok() || inspected_count != 1)
    return false;

  crtsys::wfp_kernel_http3::bounded_capsule_stream truncated;
  if (!truncated
           .consume(ntl::net::scatter_view::from_contiguous(
                        std::span<const std::byte>(*capsule_wire).first(1)),
                    inspect)
           .is_ok())
    return false;
  return truncated.finish() == STATUS_END_OF_FILE;
}

} // namespace

int main() {
  if (!crtsys::wfp_kernel_http3::supports_application_protocol("h3") ||
      crtsys::wfp_kernel_http3::supports_application_protocol("h2"))
    return 1;
  if (!request_policy_contract())
    return 2;
  if (!dynamic_qpack_contract())
    return 3;
  if (!response_contract())
    return 4;
  if (!webtransport_contract())
    return 5;
  std::cout << "kernel HTTP/3 policy contracts passed: request policy, "
               "dynamic QPACK resume/ack, gzip/deflate/br, WebTransport, "
               "bounded failures\n";
  return 0;
}
