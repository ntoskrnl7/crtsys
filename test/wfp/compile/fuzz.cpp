#include <ntl/net/tls/client_hello>

#include <ntl/net/grpc/framing>
#include <ntl/net/http/datagram>
#include <ntl/net/http/http1_framing>
#include <ntl/net/http2/framing>
#include <ntl/net/http2/hpack>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/qpack_core>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/offload/protocol>
#include <ntl/net/websocket/framing>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

constexpr std::size_t maximum_wire_size = 2048;
constexpr std::size_t maximum_decoded_size = 4096;

class fuzz_qpack_sink final : public ntl::net::http3::qpack_field_sink {
public:
  ntl::status
  on_field(ntl::net::http3::qpack_field_view field) noexcept override {
    if (field.name.size() > maximum_decoded_size - decoded_bytes ||
        field.value.size() > maximum_decoded_size - decoded_bytes -
                                 field.name.size())
      return STATUS_BUFFER_OVERFLOW;
    decoded_bytes += field.name.size() + field.value.size();
    return ntl::status::ok();
  }
  std::size_t decoded_bytes = 0;
};

class fuzz_tls_observer final
    : public ntl::net::tls_client_hello_observer {
public:
  ntl::status on_server_name(std::string_view value) noexcept override {
    server_name_size = value.size();
    return ntl::status::ok();
  }
  ntl::status
  on_application_protocol(std::string_view value) noexcept override {
    if (value.empty())
      return STATUS_DATA_ERROR;
    ++protocol_count;
    return ntl::status::ok();
  }
  std::size_t server_name_size = 0;
  std::size_t protocol_count = 0;
};

class deterministic_random {
public:
  explicit deterministic_random(
      std::uint32_t seed = 0x6d2b79f5u) noexcept
      : state_(seed == 0 ? 0x6d2b79f5u : seed) {}

  std::uint32_t next() noexcept {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

private:
  std::uint32_t state_ = 0x6d2b79f5u;
};

bool valid_probe(const ntl::net::framing::frame_probe &probe,
                 std::size_t available) noexcept {
  switch (probe.state()) {
  case ntl::net::framing::probe_state::need_more:
    return probe.required_total() > available;
  case ntl::net::framing::probe_state::complete:
    return probe.frame_size() != 0 &&
           probe.frame_size() <= available &&
           probe.content_offset() <= probe.frame_size() &&
           probe.content_size() <=
               probe.frame_size() - probe.content_offset();
  case ntl::net::framing::probe_state::malformed:
    return probe.error() != STATUS_SUCCESS;
  }
  return false;
}

bool exercise_protocols(std::span<const std::byte> storage,
                        deterministic_random &random,
                        const ntl::net::inspection::
                            content_decoder_registry &decoders) {
  std::array<std::span<const std::byte>, 8> segments{};
  std::size_t offset = 0;
  const std::size_t segment_count =
      1 + random.next() % segments.size();
  for (std::size_t index = 0; index != segment_count; ++index) {
    const std::size_t remaining = storage.size() - offset;
    const std::size_t take =
        index + 1 == segment_count
            ? remaining
            : (remaining == 0 ? 0 : random.next() % (remaining + 1));
    segments[index] = storage.subspan(offset, take);
    offset += take;
  }
  const auto bytes = ntl::net::scatter_view::from_segments(
      std::span<const std::span<const std::byte>>(segments)
          .first(segment_count));
  if (!bytes || bytes.size() != storage.size())
    return false;

  const ntl::net::runtime_descriptor offload_service{
      .domain = ntl::net::execution_domain::user,
      .path = ntl::net::execution_path::offloaded,
      .features = ntl::net::feature_set(
          ntl::net::network_feature::content_inspection |
          ntl::net::network_feature::content_transform),
      .limits = {.maximum_input_bytes = maximum_wire_size,
                 .maximum_output_bytes = maximum_decoded_size,
                 .maximum_buffered_bytes = maximum_decoded_size,
                 .timeout_milliseconds = 30'000,
                 .maximum_in_flight = 64}};
  if (storage.size() >= sizeof(ntl::net::offload::request_header)) {
    ntl::net::offload::request_header request{};
    std::memcpy(&request, storage.data(), sizeof(request));
    (void)ntl::net::offload::validate(request, offload_service);
    if (storage.size() >= sizeof(request) +
                              sizeof(ntl::net::offload::response_header)) {
      ntl::net::offload::response_header response{};
      std::memcpy(&response, storage.data() + sizeof(request),
                  sizeof(response));
      (void)ntl::net::offload::validate(response, request);
    }
  }

  const ntl::net::http::http1_framing_limits http1_limits{
      .maximum_header_size = maximum_wire_size,
      .maximum_body_size = maximum_wire_size,
      .maximum_chunk_line_size = 256,
      .maximum_trailer_size = 512,
      .allow_close_delimited_response = true};
  for (const auto kind :
       {ntl::net::http::http1_message_kind::request,
        ntl::net::http::http1_message_kind::response}) {
    const ntl::net::http::http1_message_framer framer(kind, http1_limits);
    if (!valid_probe(framer.probe(bytes), bytes.size()) ||
        !valid_probe(framer.finish(bytes), bytes.size()))
      return false;
  }

  const ntl::net::http2::frame_limits http2_limits{
      .maximum_payload_size = maximum_wire_size,
      .reject_unknown_types = false};
  const ntl::net::http2::frame_framer http2_framer(http2_limits);
  const auto http2_probe = http2_framer.probe(bytes);
  if (!valid_probe(http2_probe, bytes.size()))
    return false;
  if (http2_probe.state() ==
      ntl::net::framing::probe_state::complete) {
    const auto wire = bytes.subview(0, http2_probe.frame_size());
    if (!wire)
      return false;
    const auto frame =
        ntl::net::http2::frame_view::parse(*wire, http2_limits);
    if (!frame || frame->payload().size() >
                      http2_limits.maximum_payload_size)
      return false;
    if (frame->header().type == ntl::net::http2::frame_type::data)
      (void)frame->data_payload();
    if (frame->header().type ==
            ntl::net::http2::frame_type::headers ||
        frame->header().type ==
            ntl::net::http2::frame_type::push_promise ||
        frame->header().type ==
            ntl::net::http2::frame_type::continuation)
      (void)frame->header_block_fragment();
  }

  const ntl::net::websocket::frame_framer websocket_framer(
      ntl::net::websocket::sender_role::either,
      {.maximum_payload_size = maximum_wire_size,
       .allowed_reserved_bits = 0x07});
  const auto websocket_probe = websocket_framer.probe(bytes);
  if (!valid_probe(websocket_probe, bytes.size()))
    return false;
  if (websocket_probe.state() ==
      ntl::net::framing::probe_state::complete) {
    const auto wire = bytes.subview(0, websocket_probe.frame_size());
    if (!wire)
      return false;
    const auto header = ntl::net::websocket::inspect_header(
        *wire, ntl::net::websocket::sender_role::either,
        {maximum_wire_size, 0x07});
    if (!header || header->payload_size > maximum_wire_size)
      return false;
    const auto payload = ntl::net::websocket::decode_payload(
        *wire, *header, maximum_wire_size);
    if (!payload || payload->size() != header->payload_size)
      return false;
    std::array<std::byte, maximum_wire_size> direct_payload{};
    const auto direct = ntl::net::websocket::decode_payload_to(
        *wire, *header, direct_payload);
    if (!direct || *direct != payload->size() ||
        !std::equal(payload->begin(), payload->end(),
                    direct_payload.begin()))
      return false;
  }

  const ntl::net::grpc::message_framer grpc_framer(maximum_wire_size);
  const auto grpc_probe = grpc_framer.probe(bytes);
  if (!valid_probe(grpc_probe, bytes.size()))
    return false;
  if (grpc_probe.state() ==
      ntl::net::framing::probe_state::complete) {
    const auto wire = bytes.subview(0, grpc_probe.frame_size());
    if (!wire)
      return false;
    const auto header =
        ntl::net::grpc::inspect_header(*wire, maximum_wire_size);
    if (!header || header->payload_size != grpc_probe.content_size())
      return false;
  }

  const ntl::net::http::capsule_framer capsule_framer(
      {.maximum_payload_size = maximum_wire_size});
  if (!valid_probe(capsule_framer.probe(bytes), bytes.size()))
    return false;

  ntl::net::http2::bounded_hpack_decoder hpack(
      {.maximum_dynamic_table_size = 1024});
  const auto hpack_result =
      hpack.decode(bytes, maximum_decoded_size);
  if (hpack_result &&
      hpack_result->decoded_bytes > maximum_decoded_size)
    return false;

  const ntl::net::http3::frame_limits http3_limits{
      .maximum_payload_size = maximum_wire_size};
  const ntl::net::http3::frame_framer http3_framer(http3_limits);
  const auto http3_probe = http3_framer.probe(bytes);
  if (!valid_probe(http3_probe, bytes.size()))
    return false;
  if (http3_probe.state() ==
      ntl::net::framing::probe_state::complete) {
    const auto wire = bytes.subview(0, http3_probe.frame_size());
    if (!wire)
      return false;
    const auto frame =
        ntl::net::http3::frame_view::parse(*wire, http3_limits);
    if (!frame || frame->payload().size() >
                      http3_limits.maximum_payload_size)
      return false;
  }

  ntl::net::http3::bounded_static_qpack_decoder qpack;
  const auto qpack_result =
      qpack.decode(random.next(), bytes, maximum_decoded_size);
  if (qpack_result &&
      qpack_result->decoded_bytes > maximum_decoded_size)
    return false;
  std::array<std::byte, maximum_decoded_size> qpack_scratch{};
  fuzz_qpack_sink qpack_sink;
  const auto direct_qpack = ntl::net::http3::decode_static_qpack(
      bytes, qpack_scratch, qpack_sink, maximum_decoded_size);
  if (direct_qpack &&
      (direct_qpack->decoded_bytes > maximum_decoded_size ||
       direct_qpack->decoded_bytes != qpack_sink.decoded_bytes))
    return false;

  const ntl::net::tls_client_hello_limits tls_limits{
      .maximum_buffered_ciphertext = maximum_wire_size,
      .maximum_client_hello = maximum_wire_size - 4,
      .receive_chunk_size = 512,
      .maximum_alpn_protocols = 8};
  const auto tls = ntl::net::detail::parse_client_hello(storage, tls_limits);
  if (tls.state == ntl::net::detail::client_hello_parse_state::complete) {
    if (tls.server_name.size() > 253 ||
        tls.application_protocols.size() >
            tls_limits.maximum_alpn_protocols)
      return false;
    for (const auto &protocol : tls.application_protocols) {
      if (protocol.empty() || protocol.size() > 255)
        return false;
    }
  }
  std::array<std::byte, maximum_wire_size> tls_workspace{};
  fuzz_tls_observer tls_observer;
  const auto direct_tls = ntl::net::inspect_tls_client_hello(
      bytes, tls_workspace, tls_observer, tls_limits);
  if (direct_tls &&
      (direct_tls->handshake_size > maximum_wire_size ||
       tls_observer.server_name_size > 253 ||
       tls_observer.protocol_count > tls_limits.maximum_alpn_protocols))
    return false;

  for (const auto coding : {"gzip", "deflate", "br"}) {
    auto decoder = decoders.create(coding);
    if (!decoder)
      return false;
    const auto decoded = ntl::net::inspection::decode_complete(
        *decoder, bytes, maximum_decoded_size);
    if (decoded && decoded->size() > maximum_decoded_size)
      return false;
  }
  return true;
}

std::vector<std::vector<std::byte>> valid_seeds() {
  const auto bytes = [](const char *text) {
    const auto *begin =
        reinterpret_cast<const std::byte *>(text);
    return std::vector<std::byte>(
        begin, begin + std::char_traits<char>::length(text));
  };

  std::vector<std::vector<std::byte>> seeds;
  seeds.push_back(bytes(
      "GET / HTTP/1.1\r\nHost: example.test\r\n"
      "Content-Length: 4\r\n\r\ntest"));
  seeds.push_back(bytes(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "4\r\ntest\r\n0\r\n\r\n"));
  seeds.push_back(
      {std::byte{0}, std::byte{0}, std::byte{3}, std::byte{0},
       std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
       std::byte{1}, std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
  seeds.push_back(
      {std::byte{0}, std::byte{3}, std::byte{'a'}, std::byte{'b'},
       std::byte{'c'}});
  seeds.push_back({std::byte{0x82}});
  seeds.push_back({std::byte{0}, std::byte{0}, std::byte{0xd1}});

  std::vector<std::byte> client_hello(52, std::byte{0});
  client_hello[0] = std::byte{22};
  client_hello[1] = std::byte{3};
  client_hello[2] = std::byte{1};
  client_hello[4] = std::byte{47};
  client_hello[5] = std::byte{1};
  client_hello[8] = std::byte{43};
  client_hello[9] = std::byte{3};
  client_hello[10] = std::byte{3};
  client_hello[44] = std::byte{0};
  client_hello[46] = std::byte{2};
  client_hello[47] = std::byte{0x13};
  client_hello[48] = std::byte{0x01};
  client_hello[49] = std::byte{1};
  client_hello[50] = std::byte{0};
  seeds.push_back(std::move(client_hello));
  return seeds;
}

bool run_seed_mutations(
    deterministic_random &random,
    const ntl::net::inspection::content_decoder_registry &decoders) {
  for (const auto &seed : valid_seeds()) {
    for (std::size_t length = 0; length <= seed.size(); ++length) {
      if (!exercise_protocols(
              std::span<const std::byte>(seed).first(length), random,
              decoders))
        return false;
    }
    for (std::size_t index = 0; index != seed.size(); ++index) {
      for (const std::uint8_t mask :
           {std::uint8_t{0x01}, std::uint8_t{0x80},
            std::uint8_t{0xff}}) {
        auto mutated = seed;
        mutated[index] ^= static_cast<std::byte>(mask);
        if (!exercise_protocols(mutated, random, decoders))
          return false;
      }
    }
  }
  return true;
}

bool run_deterministic_fuzz() {
  deterministic_random random;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  if (!run_seed_mutations(random, decoders))
    return false;

  for (std::size_t iteration = 0; iteration != 16384; ++iteration) {
    const std::size_t size =
        random.next() % (maximum_wire_size + 1);
    std::vector<std::byte> storage(size);
    for (auto &value : storage)
      value = static_cast<std::byte>(random.next());
    if (!exercise_protocols(storage, random, decoders))
      return false;
  }
  return true;
}

} // namespace

#if defined(CRTSYS_WFP_LIBFUZZER)
extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t *data, std::size_t size) {
  if (size > maximum_wire_size)
    return 0;
  try {
    std::uint32_t seed = 0x6d2b79f5u;
    for (std::size_t index = 0; index != (std::min)(size, std::size_t{4});
         ++index)
      seed = (seed << 5) ^ (seed >> 2) ^ data[index];
    deterministic_random random(seed);
    static const auto decoders = [] {
      ntl::net::inspection::content_decoder_registry registry;
      ntl::net::inspection::register_standard_content_decoders(registry);
      return registry;
    }();
    const auto input = std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(data), size);
    if (!exercise_protocols(input, random, decoders))
      std::abort();
  } catch (...) {
    std::abort();
  }
  return 0;
}
#else
int main() {
  try {
    if (!run_deterministic_fuzz())
      return 1;
  } catch (...) {
    return 2;
  }
  std::cout
      << "wfp parser fuzz contracts passed: HTTP/1, HTTP/2+HPACK, "
         "HTTP/3+QPACK, WebSocket, gRPC, capsules, content-coding, "
         "TLS ClientHello, offload ABI\n";
  return 0;
}
#endif
