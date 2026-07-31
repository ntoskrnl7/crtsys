#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/http/datagram>
#include <ntl/net/http/extended_connect>
#include <ntl/net/http2/framing>
#include <ntl/net/http2/hpack>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/tls/inspection_policy>
#include <ntl/net/http3/webtransport>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/grpc/framing>
#include <ntl/net/grpc/transform>
#include <ntl/net/tls/product_policy>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/transform>

#include <brotli/encode.h>
#include <zlib.h>

namespace {

std::byte byte(char value) noexcept {
  return static_cast<std::byte>(
      static_cast<unsigned char>(value));
}

bool test_websocket() {
  const std::array<std::byte, 8> wire{
      std::byte{0x81}, std::byte{0x82},
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      byte(static_cast<char>('H' ^ 1)),
      byte(static_cast<char>('i' ^ 2))};
  const auto bytes =
      ntl::net::scatter_view::from_contiguous(wire);
  ntl::net::websocket::frame_framer framer(
      ntl::net::websocket::sender_role::client, {64, 0});
  const auto framed = framer.probe(bytes);
  if (framed.state() != ntl::net::framing::probe_state::complete ||
      framed.frame_size() != wire.size())
    return false;

  const auto header = ntl::net::websocket::inspect_header(
      bytes, ntl::net::websocket::sender_role::client, {64, 0});
  if (!header || !header->masked ||
      header->operation != ntl::net::websocket::opcode::text)
    return false;
  const auto payload =
      ntl::net::websocket::decode_payload(bytes, *header, 64);
  if (!payload || payload->size() != 2 ||
      (*payload)[0] != byte('H') || (*payload)[1] != byte('i'))
    return false;

  ntl::net::websocket::message_assembler messages(64);
  const auto message = messages.consume(*header, *payload);
  return message && *message && (*message)->payload == *payload;
}

class mock_hpack_decoder {
public:
  void reset() noexcept {}
  ntl::result<ntl::net::http2::decoded_headers>
  decode(ntl::net::scatter_view encoded,
         std::size_t maximum) noexcept {
    if (!encoded || maximum < 8)
      return ntl::unexpected(STATUS_BUFFER_OVERFLOW);
    ntl::net::http2::decoded_headers result;
    result.fields.push_back({":status", "200", false});
    result.decoded_bytes = 8;
    return ntl::ok(std::move(result));
  }
};

class http2_sink final : public ntl::net::http2::inspection_sink {
public:
  ntl::status on_headers(
      std::uint32_t, std::span<const ntl::net::http2::header_field>,
      bool) noexcept override {
    ++headers;
    return ntl::status::ok();
  }
  ntl::status on_data(
      std::uint32_t, ntl::net::scatter_view data,
      bool) noexcept override {
    data_bytes += data.size();
    return ntl::status::ok();
  }

  unsigned headers = 0;
  std::size_t data_bytes = 0;
};

std::vector<std::byte> hex_bytes(std::string_view text) {
  const auto nibble = [](char value) -> unsigned {
    if (value >= '0' && value <= '9')
      return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f')
      return static_cast<unsigned>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F')
      return static_cast<unsigned>(value - 'A' + 10);
    return 0xffu;
  };
  std::vector<std::byte> result;
  if ((text.size() & 1u) != 0)
    return result;
  result.reserve(text.size() / 2);
  for (std::size_t index = 0; index != text.size();
       index += 2) {
    const unsigned high = nibble(text[index]);
    const unsigned low = nibble(text[index + 1]);
    if (high > 0xfu || low > 0xfu)
      return {};
    result.push_back(
        static_cast<std::byte>((high << 4) | low));
  }
  return result;
}

bool hpack_fields_equal(
    const ntl::net::http2::decoded_headers &decoded,
    std::initializer_list<
        std::pair<std::string_view, std::string_view>>
        expected) {
  if (decoded.fields.size() != expected.size())
    return false;
  std::size_t index = 0;
  for (const auto &[name, value] : expected) {
    if (decoded.fields[index].name != name ||
        decoded.fields[index].value != value)
      return false;
    ++index;
  }
  return true;
}

bool test_bounded_hpack() {
  // RFC 7541 C.4: consecutive requests using Huffman coding.
  const auto first = hex_bytes(
      "828684418cf1e3c2e5f23a6ba0ab90f4ff");
  const auto second =
      hex_bytes("828684be5886a8eb10649cbf");
  const auto third = hex_bytes(
      "828785bf408825a849e95ba97d7f"
      "8925a849e95bb8e8b4bf");
  if (first.empty() || second.empty() || third.empty())
    return false;

  ntl::net::http2::bounded_hpack_decoder decoder;
  const auto decode = [&decoder](
                          const std::vector<std::byte> &wire) {
    return decoder.decode(
        ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(wire)),
        4096);
  };
  const auto one = decode(first);
  if (!one || !hpack_fields_equal(
                  *one,
                  {{":method", "GET"},
                   {":scheme", "http"},
                   {":path", "/"},
                   {":authority", "www.example.com"}}) ||
      decoder.dynamic_table_size() != 57 ||
      decoder.dynamic_table_entries() != 1)
    return false;
  const auto two = decode(second);
  if (!two || !hpack_fields_equal(
                  *two,
                  {{":method", "GET"},
                   {":scheme", "http"},
                   {":path", "/"},
                   {":authority", "www.example.com"},
                   {"cache-control", "no-cache"}}) ||
      decoder.dynamic_table_size() != 110 ||
      decoder.dynamic_table_entries() != 2)
    return false;
  const auto three = decode(third);
  if (!three || !hpack_fields_equal(
                    *three,
                    {{":method", "GET"},
                     {":scheme", "https"},
                     {":path", "/index.html"},
                     {":authority", "www.example.com"},
                     {"custom-key", "custom-value"}}) ||
      decoder.dynamic_table_size() != 164 ||
      decoder.dynamic_table_entries() != 3)
    return false;

  // RFC 7541 C.6: response context with a 256-octet table and eviction.
  const auto response_one = hex_bytes(
      "488264025885aec3771a4b6196d07abe"
      "941054d444a8200595040b8166e082a6"
      "2d1bff6e919d29ad171863c78f0b97c8"
      "e9ae82ae43d3");
  const auto response_two =
      hex_bytes("4883640effc1c0bf");
  ntl::net::http2::bounded_hpack_decoder responses({256});
  const auto response_first = responses.decode(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(response_one)),
      4096);
  if (!response_first ||
      !hpack_fields_equal(
          *response_first,
          {{":status", "302"},
           {"cache-control", "private"},
           {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
           {"location", "https://www.example.com"}}) ||
      responses.dynamic_table_size() != 222 ||
      responses.dynamic_table_entries() != 4)
    return false;
  const auto response_second = responses.decode(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(response_two)),
      4096);
  if (!response_second ||
      !hpack_fields_equal(
          *response_second,
          {{":status", "307"},
           {"cache-control", "private"},
           {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
           {"location", "https://www.example.com"}}) ||
      responses.dynamic_table_size() != 222 ||
      responses.dynamic_table_entries() != 4)
    return false;

  // A Huffman tail longer than seven bits cannot be EOS padding.
  const std::array<std::byte, 4> bad_padding{
      std::byte{0x40}, std::byte{0x81},
      std::byte{0xff}, std::byte{0x00}};
  ntl::net::http2::bounded_hpack_decoder malformed_decoder;
  const auto malformed = malformed_decoder.decode(
      ntl::net::scatter_view::from_contiguous(bad_padding), 64);
  if (malformed ||
      malformed.status() != STATUS_DATA_ERROR)
    return false;

  // Dynamic table updates are legal only before header fields.
  const std::array<std::byte, 2> late_update{
      std::byte{0x82}, std::byte{0x20}};
  const auto late = malformed_decoder.decode(
      ntl::net::scatter_view::from_contiguous(late_update), 64);
  if (late || late.status() != STATUS_DATA_ERROR)
    return false;

  ntl::net::http2::bounded_hpack_decoder limited({64});
  const std::array<std::byte, 2> oversized_update{
      std::byte{0x3f}, std::byte{0x22}};
  const auto oversized = limited.decode(
      ntl::net::scatter_view::from_contiguous(oversized_update),
      64);
  const auto output_overflow = limited.decode(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(first)),
      8);
  return !oversized &&
         oversized.status() == STATUS_BUFFER_OVERFLOW &&
         !output_overflow &&
         output_overflow.status() == STATUS_BUFFER_OVERFLOW;
}

bool test_http2() {
  const std::array<std::byte, 12> wire{
      std::byte{0}, std::byte{0}, std::byte{3},
      std::byte{0}, std::byte{1},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1},
      byte('a'), byte('b'), byte('c')};
  const auto bytes =
      ntl::net::scatter_view::from_contiguous(wire);
  ntl::net::http2::frame_framer framer({64, true});
  const auto framed = framer.probe(bytes);
  if (framed.state() != ntl::net::framing::probe_state::complete)
    return false;
  const auto frame =
      ntl::net::http2::frame_view::parse(bytes, {64, true});
  if (!frame ||
      frame->header().type != ntl::net::http2::frame_type::data ||
      !frame->header().end_stream())
    return false;
  const auto data = frame->data_payload();
  if (!data || data->size() != 3)
    return false;
  ntl::net::http2::hpack_decoder_adapter<mock_hpack_decoder>
      decoder;
  ntl::net::http2::header_block block{
      1, true, {byte('x')}};
  const auto headers =
      ntl::net::http2::decode_headers(decoder, block, 32);
  http2_sink sink;
  ntl::net::http2::connection_inspector inspector(
      decoder, 32, 32);
  const auto inspected = inspector.consume(*frame, sink);
  return headers && headers->fields.size() == 1 &&
         headers->fields[0].name == ":status" &&
         inspected.is_ok() && sink.data_bytes == 3 &&
         test_bounded_hpack();
}

class mock_qpack_decoder {
public:
  void reset() noexcept {}
  ntl::result<ntl::net::http3::decoded_headers>
  decode(std::uint64_t stream_id, ntl::net::scatter_view encoded,
         std::size_t maximum) noexcept {
    if (stream_id == 0 || !encoded || maximum < 8)
      return ntl::unexpected(STATUS_INVALID_PARAMETER);
    ntl::net::http3::decoded_headers result;
    result.fields.push_back({":status", "200", false});
    result.decoded_bytes = 8;
    return ntl::ok(std::move(result));
  }
};

class http3_sink final : public ntl::net::http3::inspection_sink {
public:
  ntl::status on_headers(
      std::uint64_t, std::span<const ntl::net::http3::header_field>)
      noexcept override {
    ++headers;
    return ntl::status::ok();
  }
  ntl::status on_data(
      std::uint64_t, ntl::net::scatter_view data) noexcept override {
    data_bytes += data.size();
    return ntl::status::ok();
  }

  unsigned headers = 0;
  std::size_t data_bytes = 0;
};

bool test_http3() {
  const std::array<std::byte, 5> wire{
      std::byte{0}, std::byte{3},
      byte('a'), byte('b'), byte('c')};
  const auto bytes =
      ntl::net::scatter_view::from_contiguous(wire);
  ntl::net::http3::frame_framer framer({64});
  const auto framed = framer.probe(bytes);
  if (framed.state() != ntl::net::framing::probe_state::complete)
    return false;
  const auto frame =
      ntl::net::http3::frame_view::parse(bytes, {64});
  if (!frame ||
      frame->header().type() !=
          ntl::net::http3::frame_type::data ||
      frame->payload().size() != 3)
    return false;
  ntl::net::http3::qpack_decoder_adapter<mock_qpack_decoder>
      decoder;
  const auto headers = ntl::net::http3::decode_header_block(
      decoder, 1, bytes, 32);
  http3_sink sink;
  ntl::net::http3::stream_inspector inspector(decoder, 32);
  const auto inspected = inspector.consume(1, *frame, sink);
  return headers && headers->fields.size() == 1 &&
         headers->fields[0].name == ":status" &&
         inspected.is_ok() && sink.data_bytes == 3;
}

bool test_static_qpack_and_http3_backend() {
  // RFC 9204 Appendix B.1: :path=/index.html using a static name.
  const auto header_block =
      hex_bytes("0000510b2f696e6465782e68746d6c");
  if (header_block.empty())
    return false;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      decoder;
  const auto decoded = decoder.decode(
      0,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(header_block)),
      4096);
  if (!decoded || decoded->fields.size() != 1 ||
      decoded->fields[0].name != ":path" ||
      decoded->fields[0].value != "/index.html")
    return false;

  std::vector<std::byte> wire{
      std::byte{0x01},
      static_cast<std::byte>(header_block.size())};
  wire.insert(
      wire.end(), header_block.begin(), header_block.end());
  wire.push_back(std::byte{0x00});
  wire.push_back(std::byte{0x03});
  wire.push_back(byte('h'));
  wire.push_back(byte('3'));
  wire.push_back(byte('!'));

  http3_sink sink;
  ntl::net::http3::connection_inspector inspector(
      decoder,
      {.maximum_concurrent_request_streams = 4,
       .maximum_buffered_bytes_per_stream = 4096,
       .frames = {4096}},
      4096);
  const std::size_t split = 5;
  const auto first = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(wire).first(split));
  const auto second = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(wire).subspan(split));
  const auto partial =
      inspector.consume_request_stream(0, first, false, sink);
  const auto complete =
      inspector.consume_request_stream(0, second, true, sink);

  const auto dynamic =
      hex_bytes("038110");
  const auto unsupported = decoder.decode(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(dynamic)),
      4096);
  constexpr ntl::net::http3::quic_backend_capabilities replay{
      .available = true,
      .tls13_termination = true,
      .destination_redirection = false,
      .qpack_dynamic_table = false,
      .encrypted_client_hello = false,
      .arbitrary_browser_server_identity = false};
  constexpr ntl::net::http3::quic_backend_capabilities browser{
      .available = true,
      .tls13_termination = true,
      .destination_redirection = true,
      .qpack_dynamic_table = true,
      .encrypted_client_hello = false,
      .arbitrary_browser_server_identity = true};
  return partial.is_ok() && complete.is_ok() &&
         sink.headers == 1 && sink.data_bytes == 3 &&
         !unsupported &&
         unsupported.status() == STATUS_NOT_SUPPORTED &&
         !replay.ready_for_transparent_browser() &&
         browser.ready_for_transparent_browser();
}

bool test_dynamic_qpack_and_blocked_stream_resume() {
  const ntl::net::http3::dynamic_qpack_limits qpack_limits{
      .maximum_table_capacity = 256,
      .maximum_blocked_streams = 2,
      .maximum_encoder_stream_buffer = 256,
      .maximum_literal_size = 4096};
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      decoder(qpack_limits);

  // Required Insert Count=1, Base=1, then dynamic relative index 0.
  const auto header_block = hex_bytes("020080");
  if (header_block.empty())
    return false;
  const auto blocked = decoder.decode(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(header_block)),
      4096);
  if (blocked || blocked.status() != STATUS_RETRY ||
      decoder.underlying().blocked_stream_count() != 1)
    return false;

  // Capacity=64, then Insert With Literal Name: x=y. Split the
  // encoder instruction to verify that fragmented control streams are
  // retained without exposing an incomplete entry.
  const auto encoder = hex_bytes("3f2141780179");
  if (encoder.size() != 6)
    return false;
  const auto first = decoder.consume_encoder_stream(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(encoder).first(3)));
  if (!first.is_ok() ||
      decoder.underlying().insert_count() != 0)
    return false;
  const auto second = decoder.consume_encoder_stream(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(encoder).subspan(3)));
  if (!second.is_ok() ||
      decoder.underlying().insert_count() != 1 ||
      decoder.underlying().dynamic_table_entries() != 1)
    return false;

  const auto decoded = decoder.decode(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(header_block)),
      4096);
  if (!decoded || decoded->fields.size() != 1 ||
      decoded->fields[0].name != "x" ||
      decoded->fields[0].value != "y" ||
      decoder.underlying().blocked_stream_count() != 0)
    return false;
  const auto acknowledgements =
      decoder.take_decoder_stream();
  if (!acknowledgements || acknowledgements->size() != 2 ||
      (*acknowledgements)[0] != std::byte{0x01} ||
      (*acknowledgements)[1] != std::byte{0x84})
    return false;

  // The same retry contract must preserve the complete HEADERS frame
  // in the connection inspector until the encoder stream catches up.
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      resumed_decoder(qpack_limits);
  std::vector<std::byte> wire{
      std::byte{0x01},
      static_cast<std::byte>(header_block.size())};
  wire.insert(
      wire.end(), header_block.begin(), header_block.end());
  http3_sink sink;
  ntl::net::http3::connection_inspector inspector(
      resumed_decoder,
      {.maximum_concurrent_request_streams = 4,
       .maximum_buffered_bytes_per_stream = 4096,
       .frames = {4096}},
      4096);
  const auto waiting = inspector.consume_request_stream(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire)),
      true, sink);
  if (waiting != STATUS_RETRY || sink.headers != 0)
    return false;
  if (!inspector
           .consume_qpack_encoder_stream(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(encoder)))
           .is_ok())
    return false;
  const auto resumed =
      inspector.resume_request_stream(4, sink);
  return resumed.is_ok() && sink.headers == 1;
}

bool test_datagrams_connect_and_webtransport() {
  const std::array<std::byte, 2> payload{
      byte('o'), byte('k')};
  const auto datagram = ntl::net::http::encode_http3_datagram(
      8, payload, {32});
  if (!datagram)
    return false;
  const auto parsed_datagram =
      ntl::net::http::http3_datagram_view::parse(
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(*datagram)),
          {32});
  if (!parsed_datagram ||
      parsed_datagram->request_stream_id() != 8 ||
      parsed_datagram->payload().size() != payload.size())
    return false;

  const auto capsule = ntl::net::http::encode_capsule(
      ntl::net::http::datagram_capsule_type, payload, {32});
  if (!capsule)
    return false;
  ntl::net::http::capsule_framer capsule_framer({32});
  const auto capsule_probe = capsule_framer.probe(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(*capsule)));
  const auto parsed_capsule = ntl::net::http::capsule_view::parse(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(*capsule)),
      {32});
  if (capsule_probe.state() !=
          ntl::net::framing::probe_state::complete ||
      !parsed_capsule || !parsed_capsule->is_datagram() ||
      parsed_capsule->payload().size() != payload.size())
    return false;

  using field = ntl::net::http3::header_field;
  const std::array<field, 6> fields{{
      {":method", "CONNECT", false},
      {":protocol", "webtransport-h3", false},
      {":scheme", "https", false},
      {":authority", "example.test", false},
      {":path", "/session", false},
      {"origin", "https://example.test", false},
  }};
  const ntl::net::http3::webtransport::prerequisites ready{
      .peer_enabled_extended_connect = true,
      .peer_enabled_webtransport = true,
      .local_h3_datagram = true,
      .peer_h3_datagram = true,
      .local_quic_datagram = true,
      .peer_quic_datagram = true,
      .local_reset_stream_at = true,
      .peer_reset_stream_at = true};
  const auto session =
      ntl::net::http3::webtransport::validate_session_request(
          std::span<const field>(fields), ready);
  if (!session || session->authority != "example.test")
    return false;

  std::vector<std::byte> stream_wire;
  if (!ntl::net::http3::append_quic_varint(
           stream_wire,
           ntl::net::http3::webtransport::
               bidirectional_stream_signal)
           .is_ok() ||
      !ntl::net::http3::append_quic_varint(stream_wire, 4)
           .is_ok())
    return false;
  stream_wire.insert(
      stream_wire.end(), payload.begin(), payload.end());
  const auto stream =
      ntl::net::http3::webtransport::parse_stream_prefix(
          ntl::net::http3::webtransport::stream_direction::
              bidirectional,
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(stream_wire)));
  if (!stream || stream->session_id != 4 ||
      stream->body.size() != payload.size())
    return false;

  const auto drain_wire = ntl::net::http::encode_capsule(
      ntl::net::http3::webtransport::wt_drain_session, {});
  if (!drain_wire)
    return false;
  const auto drain_capsule =
      ntl::net::http::capsule_view::parse(
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(*drain_wire)));
  if (!drain_capsule)
    return false;
  const auto drained =
      ntl::net::http3::webtransport::inspect_capsule(
          *drain_capsule);
  if (!drained ||
      drained->kind !=
          ntl::net::http3::webtransport::capsule_kind::
              drain_session)
    return false;

  ntl::net::http3::webtransport::session_guard guard(
      {.maximum_bidirectional_streams = 1,
       .maximum_unidirectional_streams = 1,
       .maximum_stream_data = 2,
       .maximum_datagram_payload = 2,
       .maximum_datagrams = 1});
  return guard
             .open_stream(
                 ntl::net::http3::webtransport::
                     stream_direction::bidirectional)
             .is_ok() &&
         guard.open_stream(
                   ntl::net::http3::webtransport::
                       stream_direction::bidirectional) ==
             STATUS_QUOTA_EXCEEDED &&
         guard.consume_stream_data(2).is_ok() &&
         guard.consume_stream_data(1) ==
             STATUS_QUOTA_EXCEEDED &&
         guard.consume_datagram(2).is_ok() &&
         guard.consume_datagram(1) ==
             STATUS_QUOTA_EXCEEDED;
}

class counting_decoder final
    : public ntl::net::inspection::content_decoder {
public:
  explicit counting_decoder(unsigned &count) noexcept
      : count_(&count) {}
  void reset() noexcept override {}
  ntl::net::inspection::decode_result decode(
      ntl::net::scatter_view input, bool final,
      ntl::net::inspection::decoded_output &output) noexcept override {
    ++*count_;
    const auto appended = output.append(input);
    if (!appended.is_ok())
      return ntl::net::inspection::decode_result::malformed(
          static_cast<NTSTATUS>(appended));
    return final
               ? ntl::net::inspection::decode_result::complete()
               : ntl::net::inspection::decode_result::need_more_input();
  }

private:
  unsigned *count_;
};

bool test_content_decoder() {
  ntl::net::inspection::content_decoder_registry registry;
  registry.add(
      "IDENTITY",
      [] {
        return std::make_unique<
            ntl::net::inspection::content_decoder_adapter<
                ntl::net::inspection::identity_content_decoder>>();
      });
  if (!registry.contains(" identity "))
    return false;
  auto decoder = registry.create("identity");
  if (!decoder)
    return false;
  const std::array<std::byte, 4> encoded{
      byte('t'), byte('e'), byte('s'), byte('t')};
  const auto decoded = ntl::net::inspection::decode_complete(
      *decoder, ntl::net::scatter_view::from_contiguous(encoded),
      {.maximum_encoded_size = 4,
       .maximum_decoded_size = 4,
       .maximum_expansion_ratio = 1});
  if (!decoded || *decoded !=
                      std::vector<std::byte>(
                          encoded.begin(), encoded.end()))
    return false;

  unsigned layers = 0;
  registry.add(
      "outer", [&layers] {
        return std::make_unique<counting_decoder>(layers);
      });
  registry.add(
      "inner", [&layers] {
        return std::make_unique<counting_decoder>(layers);
      });
  const auto chained =
      ntl::net::inspection::decode_content_encoding(
          registry, ntl::net::scatter_view::from_contiguous(encoded),
          "inner, outer",
          {.maximum_encoded_size = 4,
           .maximum_decoded_size = 4,
           .maximum_expansion_ratio = 1,
           .maximum_coding_layers = 2});
  const auto unsupported =
      ntl::net::inspection::decode_content_encoding(
          registry, ntl::net::scatter_view::from_contiguous(encoded),
          "missing",
          {.maximum_encoded_size = 4,
           .maximum_decoded_size = 4,
           .maximum_expansion_ratio = 1,
           .maximum_coding_layers = 2});
  return chained && *chained == *decoded && layers == 2 &&
         !unsupported &&
         unsupported.status() == STATUS_NOT_SUPPORTED;
}

std::vector<std::byte> zlib_encode(
    std::span<const std::byte> input,
    ntl::net::inspection::zlib_stream_format format) {
  z_stream stream{};
  if (::deflateInit2(
          &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
          static_cast<int>(format), 8,
          Z_DEFAULT_STRATEGY) != Z_OK)
    return {};
  struct stream_guard {
    z_stream *stream;
    ~stream_guard() { (void)::deflateEnd(stream); }
  } guard{&stream};

  std::vector<std::byte> output(
      static_cast<std::size_t>(::deflateBound(
          &stream, static_cast<uLong>(input.size()))));
  stream.next_in = reinterpret_cast<Bytef *>(
      const_cast<std::byte *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out =
      reinterpret_cast<Bytef *>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  if (::deflate(&stream, Z_FINISH) != Z_STREAM_END)
    return {};
  output.resize(output.size() - stream.avail_out);
  return output;
}

std::vector<std::byte>
brotli_encode(std::span<const std::byte> input) {
  std::vector<std::byte> output(
      ::BrotliEncoderMaxCompressedSize(input.size()));
  std::size_t output_size = output.size();
  if (::BrotliEncoderCompress(
          BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW,
          BROTLI_MODE_GENERIC, input.size(),
          reinterpret_cast<const std::uint8_t *>(input.data()),
          &output_size,
          reinterpret_cast<std::uint8_t *>(output.data())) ==
      BROTLI_FALSE)
    return {};
  output.resize(output_size);
  return output;
}

bool test_standard_content_decoders() {
  const std::string text =
      "<!doctype html><html><body>bounded compression "
      "inspection</body></html>";
  const auto plain = std::as_bytes(std::span(text));
  const auto gzip = zlib_encode(
      plain, ntl::net::inspection::zlib_stream_format::gzip);
  const auto deflate = zlib_encode(
      plain, ntl::net::inspection::zlib_stream_format::zlib);
  const auto brotli = brotli_encode(plain);
  const auto nested = brotli_encode(gzip);
  if (gzip.empty() || deflate.empty() || brotli.empty() ||
      nested.empty())
    return false;

  ntl::net::inspection::content_decoder_registry registry;
  ntl::net::inspection::register_standard_content_decoders(registry);
  const ntl::net::inspection::decode_limits limits{
      4096, 4096, 128, 4};
  const auto decode = [&registry, limits](
                          const std::vector<std::byte> &wire,
                          std::string_view encoding) {
    return ntl::net::inspection::decode_content_encoding(
        registry,
        ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(wire)),
        encoding, limits);
  };
  const auto decoded_gzip = decode(gzip, "gzip");
  const auto decoded_deflate = decode(deflate, "deflate");
  const auto decoded_brotli = decode(brotli, "br");
  const auto decoded_nested = decode(nested, "gzip, br");
  const std::vector<std::byte> expected(
      plain.begin(), plain.end());
  if (!decoded_gzip || *decoded_gzip != expected ||
      !decoded_deflate || *decoded_deflate != expected ||
      !decoded_brotli || *decoded_brotli != expected ||
      !decoded_nested || *decoded_nested != expected)
    return false;

  auto corrupted_gzip = gzip;
  corrupted_gzip.back() ^= std::byte{0x01};
  auto truncated_brotli = brotli;
  truncated_brotli.pop_back();
  const auto checksum = decode(corrupted_gzip, "gzip");
  const auto truncated = decode(truncated_brotli, "br");
  const auto too_small =
      ntl::net::inspection::decode_content_encoding(
          registry,
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(gzip)),
          "gzip",
          {.maximum_encoded_size = 4096,
           .maximum_decoded_size = 8,
           .maximum_expansion_ratio = 128,
           .maximum_coding_layers = 4});
  const auto too_many_layers =
      ntl::net::inspection::decode_content_encoding(
          registry,
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(nested)),
          "gzip, br",
          {.maximum_encoded_size = 4096,
           .maximum_decoded_size = 4096,
           .maximum_expansion_ratio = 128,
           .maximum_coding_layers = 1});
  return !checksum && checksum.status() == STATUS_DATA_ERROR &&
         !truncated &&
         truncated.status() == STATUS_END_OF_FILE &&
         !too_small &&
         too_small.status() == STATUS_BUFFER_OVERFLOW &&
         !too_many_layers &&
         too_many_layers.status() == STATUS_BUFFER_OVERFLOW;
}

std::vector<std::vector<std::byte>>
permessage_deflate_encode(
    std::initializer_list<std::string_view> messages) {
  z_stream stream{};
  if (::deflateInit2(
          &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
          -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    return {};
  struct stream_guard {
    z_stream *stream;
    ~stream_guard() { (void)::deflateEnd(stream); }
  } guard{&stream};

  std::vector<std::vector<std::byte>> result;
  for (const std::string_view message : messages) {
    std::vector<std::byte> encoded(
        static_cast<std::size_t>(::deflateBound(
            &stream, static_cast<uLong>(message.size()))) +
        16);
    stream.next_in = reinterpret_cast<Bytef *>(
        const_cast<char *>(message.data()));
    stream.avail_in = static_cast<uInt>(message.size());
    stream.next_out =
        reinterpret_cast<Bytef *>(encoded.data());
    stream.avail_out = static_cast<uInt>(encoded.size());
    if (::deflate(&stream, Z_SYNC_FLUSH) != Z_OK)
      return {};
    encoded.resize(encoded.size() - stream.avail_out);
    constexpr std::array<std::byte, 4> tail{
        std::byte{0x00}, std::byte{0x00},
        std::byte{0xff}, std::byte{0xff}};
    if (encoded.size() < tail.size() ||
        !std::equal(
            tail.begin(), tail.end(),
            encoded.end() -
                static_cast<std::ptrdiff_t>(tail.size())))
      return {};
    encoded.resize(encoded.size() - tail.size());
    result.push_back(std::move(encoded));
  }
  return result;
}

bool test_websocket_permessage_deflate() {
  const auto negotiated =
      ntl::net::websocket::parse_permessage_deflate_response(
          "permessage-deflate; client_max_window_bits=15; "
          "server_max_window_bits=\"15\"");
  const auto duplicate =
      ntl::net::websocket::parse_permessage_deflate_response(
          "permessage-deflate; client_no_context_takeover; "
          "client_no_context_takeover");
  const auto unknown =
      ntl::net::websocket::parse_permessage_deflate_response(
          "permessage-deflate; unknown=1");
  if (!negotiated || !negotiated->enabled ||
      negotiated->client_max_window_bits != 15 ||
      negotiated->server_max_window_bits != 15 ||
      duplicate ||
      duplicate.status() != STATUS_DATA_ERROR ||
      unknown || unknown.status() != STATUS_NOT_SUPPORTED)
    return false;

  const std::string first =
      "shared compression dictionary shared compression dictionary";
  const std::string second =
      "shared compression dictionary suffix";
  const auto encoded =
      permessage_deflate_encode({first, second});
  if (encoded.size() != 2)
    return false;
  ntl::net::websocket::permessage_deflate_decoder decoder(
      15, false, 4096);
  const auto decoded_first = decoder.decode(encoded[0], true);
  const auto decoded_second = decoder.decode(encoded[1], true);
  if (!decoded_first || !decoded_second ||
      *decoded_first !=
          std::vector<std::byte>(
              std::as_bytes(std::span(first)).begin(),
              std::as_bytes(std::span(first)).end()) ||
      *decoded_second !=
          std::vector<std::byte>(
              std::as_bytes(std::span(second)).begin(),
              std::as_bytes(std::span(second)).end()))
    return false;

  ntl::net::websocket::message_assembler assembler(4096);
  const std::array<std::byte, 1> first_fragment{
      std::byte{0x01}};
  const std::array<std::byte, 1> second_fragment{
      std::byte{0x02}};
  const ntl::net::websocket::frame_header first_header{
      false, 0x04, ntl::net::websocket::opcode::text,
      false, 1, 2, {}};
  const ntl::net::websocket::frame_header second_header{
      true, 0, ntl::net::websocket::opcode::continuation,
      false, 1, 2, {}};
  const auto partial =
      assembler.consume(first_header, first_fragment);
  const auto complete =
      assembler.consume(second_header, second_fragment);
  if (!partial || partial->has_value() || !complete ||
      !*complete || !(*complete)->compressed())
    return false;

  ntl::net::websocket::permessage_deflate_decoder limited(
      15, true, 8);
  const auto oversized = limited.decode(encoded[0], true);
  return !oversized &&
         oversized.status() == STATUS_BUFFER_OVERFLOW;
}

bool test_websocket_transform() {
  namespace websocket = ntl::net::websocket;
  websocket::message_transform_pipeline pipeline({1024, 4096, 4096, 4096, true});
  pipeline.transform([](websocket::message &message) {
    if (message.operation != websocket::opcode::text)
      return websocket::rewrite_result::unchanged();
    return websocket::rewrite_result::replace(
        std::vector<std::byte>{byte('B'), byte('y'), byte('e')});
  });
  websocket::wire_transformer transformer(
      websocket::sender_role::client, pipeline, std::nullopt,
      [] { return std::array<std::byte, 4>{
                 std::byte{5}, std::byte{6},
                 std::byte{7}, std::byte{8}}; });
  const std::array<std::byte, 8> input{
      std::byte{0x81}, std::byte{0x82},
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      byte(static_cast<char>('H' ^ 1)),
      byte(static_cast<char>('i' ^ 2))};
  auto outcome = transformer.consume(
      ntl::net::scatter_view::from_contiguous(input));
  if (outcome.action != websocket::rewrite_action::forward ||
      !outcome.modified || outcome.wire.empty())
    return false;
  const auto view = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(outcome.wire));
  const auto header = websocket::inspect_header(
      view, websocket::sender_role::client, {4096, 0});
  if (!header || !header->masked)
    return false;
  const auto decoded = websocket::decode_payload(view, *header, 4096);
  return decoded && *decoded ==
      std::vector<std::byte>{byte('B'), byte('y'), byte('e')};
}

bool test_grpc_transform() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_encoders(encoders);

  ntl::net::grpc::message_transform_pipeline pipeline;
  pipeline.transform([](ntl::net::grpc::semantic_message &message) {
    message.payload.push_back(byte('!'));
    return ntl::net::grpc::transform_result::replace(
        std::move(message.payload));
  });
  const std::array<std::byte, 2> plain{byte('o'), byte('k')};
  auto gzip = encoders.create("gzip");
  if (!gzip)
    return false;
  auto compressed = ntl::net::inspection::encode_complete(
      *gzip, plain, 4096);
  if (!compressed)
    return false;
  auto wire = ntl::net::grpc::encode_message(*compressed, true, 4096);
  if (!wire)
    return false;

  ntl::net::grpc::stream_transformer transformer(
      ntl::net::grpc::direction::response, "gzip", pipeline,
      decoders, encoders);
  auto first = transformer.feed(
      std::span<const std::byte>(*wire).first(3), false);
  if (first.action != ntl::net::grpc::transform_action::forward ||
      first.messages != 0 || !first.wire.empty())
    return false;
  auto second = transformer.feed(
      std::span<const std::byte>(*wire).subspan(3), true);
  if (second.action != ntl::net::grpc::transform_action::forward ||
      second.messages != 1 || second.modified_messages != 1)
    return false;
  const auto output_view = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(second.wire));
  const auto header = ntl::net::grpc::inspect_header(output_view, 4096);
  if (!header || !header->compressed)
    return false;
  auto decoder = decoders.create("gzip");
  if (!decoder)
    return false;
  auto decoded = ntl::net::inspection::decode_complete(
      *decoder,
      *output_view.subview(5, header->payload_size), 4096);
  return decoded && *decoded ==
      std::vector<std::byte>{byte('o'), byte('k'), byte('!')};
}

bool test_webtransport_transform() {
  namespace webtransport = ntl::net::http3::webtransport;
  webtransport::transform_session session;
  session.transform([](webtransport::payload &value) {
    if (value.kind == webtransport::payload_kind::stream) {
      auto bytes = value.bytes;
      bytes.push_back(byte('!'));
      return webtransport::transform_result::replace(std::move(bytes));
    }
    return webtransport::transform_result::unchanged();
  });
  if (!session.open_stream(
          webtransport::stream_direction::bidirectional).is_ok())
    return false;
  webtransport::payload stream{
      webtransport::payload_kind::stream, 0,
      webtransport::stream_direction::bidirectional, 0,
      {byte('w'), byte('t')}};
  const auto rewritten = session.apply(stream);
  if (rewritten.action != webtransport::transform_action::forward ||
      !rewritten.modified || stream.bytes.size() != 3)
    return false;

  webtransport::transform_session capsule_session;
  capsule_session.transform([](webtransport::payload &value) {
    auto replacement = value.bytes;
    replacement.push_back(std::byte{0});
    return webtransport::transform_result::replace(
        std::move(replacement));
  });
  webtransport::payload capsule{
      webtransport::payload_kind::capsule, 0,
      webtransport::stream_direction::bidirectional,
      webtransport::wt_drain_session, {}};
  const auto denied = capsule_session.apply(capsule);
  return denied.action == webtransport::transform_action::block_session &&
         denied.failure == STATUS_ACCESS_DENIED;
}

class recording_quic_backend final
    : public ntl::net::http3::quic_transport_backend {
public:
  struct write_record {
    std::uint64_t stream_id = 0;
    std::vector<std::byte> bytes;
    bool final = false;
  };
  struct reset_record {
    std::uint64_t stream_id = 0;
    std::uint64_t error_code = 0;
    std::uint64_t reliable_size = 0;
  };

  ntl::net::http3::quic_backend_capabilities
  capabilities() const noexcept override {
    return {.available = true,
            .tls13_termination = true,
            .qpack_dynamic_table = false,
            .bidirectional_streams = true,
            .unidirectional_streams = true,
            .quic_datagrams = true,
            .reliable_reset_at = true,
            .extended_connect = true,
            .webtransport = true};
  }
  ntl::status run(ntl::net::http3::quic_backend_sink &) noexcept override {
    return ntl::status::ok();
  }
  ntl::status write_stream(
      std::uint64_t stream_id, ntl::net::scatter_view plaintext,
      bool final) noexcept override {
    try {
      write_record record{stream_id, std::vector<std::byte>(plaintext.size()),
                          final};
      if (!record.bytes.empty()) {
        const auto copied = plaintext.copy_to(record.bytes);
        if (!copied.is_ok())
          return copied;
      }
      writes.push_back(std::move(record));
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }
  ntl::status open_bidirectional_stream(
      std::uint64_t &stream_id) noexcept override {
    stream_id = next_bidirectional;
    next_bidirectional += 4;
    return ntl::status::ok();
  }
  ntl::status open_request_stream(
      std::uint64_t &stream_id) noexcept override {
    return open_bidirectional_stream(stream_id);
  }
  ntl::status open_unidirectional_stream(
      std::uint64_t &stream_id) noexcept override {
    stream_id = next_unidirectional;
    next_unidirectional += 4;
    return ntl::status::ok();
  }
  ntl::status send_datagram(
      ntl::net::scatter_view plaintext) noexcept override {
    try {
      datagram.resize(plaintext.size());
      return datagram.empty() ? ntl::status::ok()
                              : plaintext.copy_to(datagram);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }
  ntl::status reset_stream_at(
      std::uint64_t stream_id, std::uint64_t error_code,
      std::uint64_t reliable_size) noexcept override {
    resets.push_back({stream_id, error_code, reliable_size});
    return ntl::status::ok();
  }
  void stop() noexcept override {}

  std::vector<write_record> writes;
  std::vector<reset_record> resets;
  std::vector<std::byte> datagram;
  std::uint64_t next_bidirectional = 0;
  std::uint64_t next_unidirectional = 2;
};

bool test_webtransport_backend_session() {
  namespace wt = ntl::net::http3::webtransport;
  recording_quic_backend backend;
  wt::backend_session client(backend);
  client.set_negotiated_transport({true, true});
  if (!client.send_local_settings(false).is_ok() || backend.writes.size() != 1)
    return false;
  const auto settings = wt::parse_control_stream(
      ntl::net::scatter_view::from_contiguous(backend.writes[0].bytes));
  if (!settings || !settings->client_ready() || settings->extended_connect ||
      backend.writes[0].final)
    return false;

  if (!client.open_client({.authority = "example.test:443",
                           .path = "/transport",
                           .origin = "https://example.test"})
           .is_ok() ||
      !client.active() || client.session_id() != 0 ||
      backend.writes.size() != 2 || backend.writes[1].final)
    return false;
  const auto connect_frame = ntl::net::http3::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(backend.writes[1].bytes));
  if (!connect_frame ||
      connect_frame->header().type() != ntl::net::http3::frame_type::headers)
    return false;
  ntl::net::http3::bounded_static_qpack_decoder decoder;
  auto connect_headers = decoder.decode(
      0, connect_frame->payload(), 64 * 1024);
  if (!connect_headers)
    return false;
  const wt::prerequisites prerequisites{true, true, true, true,
                                        true, true, true, true};
  if (!wt::validate_session_request(
          std::span<const ntl::net::http3::header_field>(
              connect_headers->fields), prerequisites))
    return false;

  const std::array<std::byte, 3> payload{byte('w'), byte('t'), byte('!')};
  auto bidi_stream = client.open_bidirectional_stream();
  if (!bidi_stream || bidi_stream->reliable_prefix_size() == 0 ||
      !client.write(*bidi_stream, std::span(payload).first(1)).is_ok() ||
      !client.write(*bidi_stream, std::span(payload).subspan(1), true).is_ok() ||
      bidi_stream->active())
    return false;
  auto uni_stream = client.open_unidirectional_stream();
  if (!uni_stream ||
      !client.write(*uni_stream, payload, true).is_ok() ||
      !client.send_datagram(payload).is_ok())
    return false;
  const auto bidi = wt::parse_stream_prefix(
      wt::stream_direction::bidirectional,
      ntl::net::scatter_view::from_contiguous(backend.writes[2].bytes));
  const auto uni = wt::parse_stream_prefix(
      wt::stream_direction::unidirectional,
      ntl::net::scatter_view::from_contiguous(backend.writes[5].bytes));
  const auto datagram = ntl::net::http::http3_datagram_view::parse(
      ntl::net::scatter_view::from_contiguous(backend.datagram));
  if (!bidi || !uni || !datagram || bidi->session_id != 0 ||
      uni->session_id != 0 || datagram->request_stream_id() != 0 ||
      !bidi->body.empty() || !uni->body.empty() ||
      backend.writes[3].bytes.size() != 1 ||
      backend.writes[4].bytes.size() != 2 || !backend.writes[4].final ||
      backend.writes[6].bytes.size() != payload.size() ||
      !backend.writes[6].final)
    return false;

  auto reset_stream = client.open_bidirectional_stream();
  if (!reset_stream ||
      !client.write(*reset_stream, std::span(payload).first(1)).is_ok() ||
      !client.reset(*reset_stream, 0x10203040).is_ok() ||
      reset_stream->active() || backend.resets.size() != 1 ||
      backend.resets[0].stream_id != reset_stream->id() ||
      backend.resets[0].reliable_size !=
          reset_stream->reliable_prefix_size() ||
      backend.resets[0].error_code !=
          wt::application_error_to_http3(0x10203040))
    return false;
  const auto mapped_zero = wt::application_error_from_http3(
      wt::application_error_to_http3(0));
  const auto mapped_max = wt::application_error_from_http3(
      wt::application_error_to_http3(
          (std::numeric_limits<std::uint32_t>::max)()));
  if (!mapped_zero || *mapped_zero != 0 || !mapped_max ||
      *mapped_max != (std::numeric_limits<std::uint32_t>::max)() ||
      wt::application_error_from_http3(
          wt::application_error_to_http3(30) - 1))
    return false;

  recording_quic_backend server_backend;
  wt::backend_session server(server_backend);
  server.set_negotiated_transport({true, true});
  if (!server.send_local_settings(true).is_ok() ||
      !server.accept_server(0).is_ok() || server_backend.writes.size() != 2)
    return false;
  const auto server_settings = wt::parse_control_stream(
      ntl::net::scatter_view::from_contiguous(server_backend.writes[0].bytes));
  const auto response_frame = ntl::net::http3::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(server_backend.writes[1].bytes));
  if (!server_settings || !server_settings->server_ready() ||
      !response_frame)
    return false;
  auto response_headers = decoder.decode(0, response_frame->payload(), 4096);
  return response_headers && response_headers->fields.size() == 1 &&
         response_headers->fields[0].name == ":status" &&
         response_headers->fields[0].value == "200";
}

bool test_product_inspection_policy() {
  using namespace ntl::net::inspection;
  product_inspection_policy policy;
  tls_inspection_observation observation;
  observation.transport = encrypted_transport::quic;
  observation.server_name = L"example.test";
  observation.protocol_adapter_available = true;
  observation.quic_backend_available = false;
  inspection_capabilities capabilities;
  capabilities.transparent_tcp_redirect = true;
  capabilities.tcp_tls_frontend = true;
  capabilities.managed_downstream_identity = true;
  const auto fallback = policy.plan(observation, capabilities);
  if (fallback.enforcement !=
          inspection_enforcement::block_quic_for_tcp_fallback ||
      !fallback.expects_tcp_retry)
    return false;
  observation.transport = encrypted_transport::tcp_tls;
  observation.encrypted_client_hello_confirmed = true;
  const auto ech = policy.plan(observation, capabilities);
  if (ech.enforcement != inspection_enforcement::terminate ||
      ech.issue != tls_inspection_issue::encrypted_client_hello)
    return false;
  observation.encrypted_client_hello_confirmed = false;
  observation.protocol = application_protocol::http2;
  capabilities.http2_adapter = true;
  const auto inspect = policy.plan(observation, capabilities);
  if (inspect.enforcement != inspection_enforcement::intercept)
    return false;

  product_inspection_options optional;
  optional.requirement = inspection_requirement::when_possible;
  product_inspection_policy optional_policy(optional);
  observation.downstream_rejected_issued_certificate = true;
  const auto pinned = optional_policy.plan(observation, capabilities);
  return pinned.enforcement == inspection_enforcement::tunnel_unchanged &&
         pinned.preserves_ciphertext &&
         pinned.issue == tls_inspection_issue::certificate_pinning_rejected;
}


bool test_tls_policy() {
  const auto h2 =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::tcp_tls, "h2");
  const auto h3 =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::quic, "h3-29");
  const auto invalid =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::tcp_tls, "h3");
  const auto fallback =
      ntl::net::inspection::select_tls_application_protocol(
          ntl::net::inspection::encrypted_transport::tcp_tls, "", true);
  if (h2.protocol !=
          ntl::net::inspection::application_protocol::http2 ||
      h3.protocol !=
          ntl::net::inspection::application_protocol::http3 ||
      invalid.valid_for_transport ||
      fallback.protocol !=
          ntl::net::inspection::application_protocol::http1 ||
      fallback.negotiated)
    return false;

  const ntl::net::inspection::explicit_tls_inspection_policy policy;
  ntl::net::inspection::tls_inspection_observation observation;
  observation.server_name = L"example.test";
  observation.encrypted_client_hello_confirmed = true;
  observation.protocol_adapter_available = true;
  const auto ech = policy.decide(observation);
  if (ech.action != ntl::net::inspection::tls_inspection_action::block ||
      ech.issue !=
          ntl::net::inspection::tls_inspection_issue::
              encrypted_client_hello)
    return false;
  observation.encrypted_client_hello_confirmed = false;
  observation.downstream_trust_known = false;
  const auto unknown_trust = policy.decide(observation);
  if (unknown_trust.action !=
          ntl::net::inspection::tls_inspection_action::block ||
      unknown_trust.issue !=
          ntl::net::inspection::tls_inspection_issue::
              downstream_trust_unknown)
    return false;
  observation.downstream_trust_known = true;
  observation.protocol =
      ntl::net::inspection::application_protocol::http2;
  const auto inspect = policy.decide(observation);
  return inspect.action ==
             ntl::net::inspection::tls_inspection_action::inspect &&
         inspect.issue ==
             ntl::net::inspection::tls_inspection_issue::none;
}

} // namespace

int main() {
  if (!test_websocket())
    return 1;
  if (!test_http2())
    return 2;
  if (!test_http3())
    return 3;
  if (!test_static_qpack_and_http3_backend())
    return 4;
  if (!test_dynamic_qpack_and_blocked_stream_resume())
    return 5;
  if (!test_datagrams_connect_and_webtransport())
    return 6;
  if (!test_content_decoder())
    return 7;
  if (!test_standard_content_decoders())
    return 8;
  if (!test_websocket_permessage_deflate())
    return 9;
  if (!test_tls_policy())
    return 10;
  if (!test_websocket_transform())
    return 11;
  if (!test_grpc_transform())
    return 12;
  if (!test_webtransport_transform())
    return 13;
  if (!test_product_inspection_policy())
    return 14;
  if (!test_webtransport_backend_session())
    return 15;
  std::cout
      << "NTL protocol adapters ok: websocket, http2, http3, "
         "static/dynamic-qpack, blocked-stream-resume, "
         "datagram, capsule, extended-connect, "
         "webtransport-draft16, quic-backend, permessage-deflate, "
         "gzip, deflate, br, "
         "content-decoder, tls-policy, websocket-transform, "
         "grpc-transform, webtransport-transform, webtransport-session, "
         "product-policy\n";
  return 0;
}
