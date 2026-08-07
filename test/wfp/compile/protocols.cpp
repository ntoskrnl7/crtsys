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
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http2/framing>
#include <ntl/net/http2/hpack>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/qpack_core>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/tls/inspection_policy>
#include <ntl/net/http3/webtransport>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/io/transport>
#include <ntl/net/grpc/framing>
#include <ntl/net/grpc/transform>
#include <ntl/net/offload/async_backend>
#include <ntl/net/offload/backend>
#include <ntl/net/offload/inspect_adapter>
#include <ntl/net/offload/protocol>
#include <ntl/net/quic/borrowed_callback_transport>
#include <ntl/net/quic/transport>
#include <ntl/net/runtime>
#include <ntl/net/tls/client_hello_parser>
#include <ntl/net/tls/product_policy>
#include <ntl/net/transform_pipeline>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/stream_transform>
#include <ntl/net/websocket/transform>

#include <brotli/encode.h>
#include <zlib.h>

namespace {

constexpr std::byte byte(char value) noexcept {
  return static_cast<std::byte>(
      static_cast<unsigned char>(value));
}

void append_u16(std::vector<std::byte> &output, std::size_t value) {
  output.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
  output.push_back(static_cast<std::byte>(value & 0xffu));
}

class observed_client_hello final
    : public ntl::net::tls_client_hello_observer {
public:
  ntl::status on_server_name(std::string_view value) noexcept override {
    server_name.assign(value);
    return ntl::status::ok();
  }
  ntl::status
  on_application_protocol(std::string_view value) noexcept override {
    protocols.emplace_back(value);
    return ntl::status::ok();
  }

  std::string server_name;
  std::vector<std::string> protocols;
};

class observed_qpack final : public ntl::net::http3::qpack_field_sink {
public:
  ntl::status
  on_field(ntl::net::http3::qpack_field_view field) noexcept override {
    fields.emplace_back(field.name, field.value);
    return ntl::status::ok();
  }

  std::vector<std::pair<std::string, std::string>> fields;
};

class observed_transport final : public ntl::net::io::transport_sink {
public:
  ntl::status on_receive(ntl::net::scatter_view, bool) noexcept override {
    return ntl::status::ok();
  }
  void on_write_complete(std::uint64_t, NTSTATUS,
                         std::size_t) noexcept override {}
  void on_closed(NTSTATUS) noexcept override {}
};

struct transport_state {
  ntl::net::io::transport_sink *sink = nullptr;
  std::uint64_t operation_id = 0;
  std::size_t written = 0;
  bool stopped = false;
};

ntl::status start_transport(
    void *context, ntl::net::io::transport_sink &sink) noexcept {
  static_cast<transport_state *>(context)->sink = &sink;
  return ntl::status::ok();
}

ntl::status write_transport(void *context, std::uint64_t operation_id,
                            ntl::net::scatter_view bytes,
                            bool) noexcept {
  auto &state = *static_cast<transport_state *>(context);
  state.operation_id = operation_id;
  state.written = bytes.size();
  if (state.sink)
    state.sink->on_write_complete(operation_id, STATUS_SUCCESS, bytes.size());
  return ntl::status::ok();
}

ntl::status cancel_transport(void *, std::uint64_t) noexcept {
  return ntl::status::ok();
}

void stop_transport(void *context) noexcept {
  static_cast<transport_state *>(context)->stopped = true;
}

ntl::status drain_transport(void *) noexcept { return ntl::status::ok(); }

class observed_quic final : public ntl::net::quic::backend_sink {
public:
  ntl::status on_connected(std::string_view) noexcept override {
    connected = true;
    return ntl::status::ok();
  }
  ntl::status on_request_stream(std::uint64_t, ntl::net::scatter_view,
                                bool) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }
  void on_closed(NTSTATUS) noexcept override {}

  bool connected = false;
};

struct quic_transport_state {
  ntl::net::quic::backend_sink *sink = nullptr;
  std::size_t written = 0;
  bool stopped = false;
  bool drained = false;
};

ntl::status run_quic_transport(
    void *context, ntl::net::quic::backend_sink &sink) noexcept {
  auto &state = *static_cast<quic_transport_state *>(context);
  state.sink = &sink;
  return sink.on_connected("h3");
}

ntl::status write_quic_transport(void *context, std::uint64_t,
                                 ntl::net::scatter_view bytes,
                                 bool) noexcept {
  static_cast<quic_transport_state *>(context)->written = bytes.size();
  return ntl::status::ok();
}

void stop_quic_transport(void *context) noexcept {
  static_cast<quic_transport_state *>(context)->stopped = true;
}

ntl::status drain_quic_transport(void *context) noexcept {
  auto &state = *static_cast<quic_transport_state *>(context);
  if (!state.stopped)
    return STATUS_INVALID_DEVICE_STATE;
  state.drained = true;
  return ntl::status::ok();
}

class observed_offload_completion final
    : public ntl::net::offload::completion_sink {
public:
  void on_complete(
      const ntl::net::offload::response_header &value) noexcept override {
    response = value;
    completed = true;
  }

  ntl::net::offload::response_header response{};
  bool completed = false;
};

struct async_offload_state {
  std::uint64_t cancelled = 0;
  bool stopped = false;
  bool drained = false;
};

ntl::status submit_async_offload(
    void *, ntl::net::offload::request_header request,
    ntl::net::scatter_view, std::span<std::byte>,
    ntl::net::offload::completion_sink &sink) noexcept {
  sink.on_complete({
      .kind = request.kind,
      .request_id = request.request_id,
      .completion_status = STATUS_SUCCESS,
      .verdict = ntl::net::inspection::verdict::block,
  });
  return ntl::status::ok();
}

ntl::status cancel_async_offload(void *context,
                                 std::uint64_t request_id) noexcept {
  static_cast<async_offload_state *>(context)->cancelled = request_id;
  return ntl::status::ok();
}

void stop_async_offload(void *context) noexcept {
  static_cast<async_offload_state *>(context)->stopped = true;
}

ntl::status drain_async_offload(void *context) noexcept {
  auto &state = *static_cast<async_offload_state *>(context);
  if (!state.stopped)
    return STATUS_INVALID_DEVICE_STATE;
  state.drained = true;
  return ntl::status::ok();
}

ntl::status execute_inspection(
    void *, const ntl::net::offload::request_header &request,
    ntl::net::scatter_view, std::span<std::byte>,
    ntl::net::offload::response_header &response) noexcept {
  response = {
      .kind = request.kind,
      .request_id = request.request_id,
      .completion_status = STATUS_SUCCESS,
      .verdict = ntl::net::inspection::verdict::block,
  };
  return ntl::status::ok();
}

bool test_dual_runtime_core() {
  static_assert(ntl::net::native_execution_domain ==
                ntl::net::execution_domain::user);
  const auto fail = [](std::string_view stage) {
    std::cerr << "dual-runtime stage failed: " << stage << '\n';
    return false;
  };

  constexpr std::array<std::byte, 3> payload{
      byte('n'), byte('t'), byte('l')};
  std::array<std::byte, 64> output{};
  const auto grpc = ntl::net::grpc::encode_message_to(
      output, payload, true, payload.size());
  if (!grpc || *grpc != payload.size() + 5)
    return false;
  std::array<std::byte, 4> undersized_output{};
  if (ntl::net::grpc::encode_message_to(
          undersized_output, payload, true, payload.size()))
    return fail("gRPC output bound");
  const auto grpc_header = ntl::net::grpc::inspect_header(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(output).first(*grpc)),
      payload.size());
  if (!grpc_header || !grpc_header->compressed ||
      grpc_header->payload_size != payload.size())
    return false;

  transport_state transport_context;
  ntl::net::io::borrowed_callback_transport transport(
      &transport_context,
      {&start_transport, &write_transport, &cancel_transport,
       &stop_transport, &drain_transport},
      {.available = true,
       .full_duplex = true,
       .half_close = true,
       .cancellation = true},
      {.domain = ntl::net::execution_domain::user,
       .path = ntl::net::execution_path::direct,
       .features = ntl::net::feature_set(
           ntl::net::network_feature::byte_transport),
       .limits = {.maximum_input_bytes = 64}});
  observed_transport transport_sink;
  const auto payload_view = ntl::net::scatter_view::from_contiguous(payload);
  if (!transport.start_borrowed(transport_sink).is_ok() ||
      !transport.write(7, payload_view, false).is_ok() ||
      transport_context.operation_id != 7 ||
      transport_context.written != payload.size() ||
      !transport.cancel(7).is_ok() || !transport.drain().is_ok())
    return false;
  transport.stop();
  if (!transport_context.stopped)
    return false;

  auto invalid_runtime = transport.runtime();
  invalid_runtime.path = static_cast<ntl::net::execution_path>(0xff);
  if (invalid_runtime.valid() ||
      invalid_runtime.supports(ntl::net::network_feature::byte_transport,
                               invalid_runtime.path))
    return fail("invalid runtime descriptor");

  quic_transport_state quic_context;
  ntl::net::quic::borrowed_callback_transport quic_transport(
      &quic_context,
      {.run_borrowed = &run_quic_transport,
       .write_stream = &write_quic_transport,
       .stop = &stop_quic_transport,
       .drain = &drain_quic_transport},
      {.available = true, .tls13_termination = true,
       .bidirectional_streams = true},
      {.domain = ntl::net::execution_domain::user,
       .path = ntl::net::execution_path::direct,
       .features = ntl::net::feature_set(
           ntl::net::network_feature::quic_transport),
       .limits = {.maximum_input_bytes = 64}});
  observed_quic quic_sink;
  if (!quic_transport.run_borrowed(quic_sink).is_ok() || !quic_sink.connected ||
      !quic_transport.write_stream(0, payload_view, false).is_ok() ||
      quic_context.written != payload.size())
    return false;
  if (!quic_sink
           .on_peer_certificate(
               ntl::net::quic::peer_certificate_view{})
           .is_ok() ||
      quic_sink
          .on_peer_certificate(
              {.deferred_error_flags = 1,
               .deferred_status = STATUS_ACCESS_DENIED})
          .is_ok())
    return fail("QUIC peer-certificate policy did not fail closed");
  quic_transport.stop();
  if (!quic_transport.drain().is_ok() || !quic_context.stopped ||
      !quic_context.drained)
    return false;

  const ntl::net::runtime_descriptor inspection_service{
      .domain = ntl::net::execution_domain::user,
      .path = ntl::net::execution_path::offloaded,
      .features = ntl::net::feature_set(
          ntl::net::network_feature::content_inspection),
      .limits = {.maximum_input_bytes = 64,
                 .maximum_output_bytes = 64,
                 .maximum_buffered_bytes = 64,
                 .timeout_milliseconds = 1000,
                 .maximum_in_flight = 4}};
  const auto inspection_request = ntl::net::offload::make_request(
      ntl::net::offload::operation::inspect_content, 41,
      {.kind = ntl::net::inspection::content_kind::tcp_message,
       .flow_direction = ntl::net::inspection::direction::inbound,
       .flow_id = 9,
       .source_port = 1234,
       .destination_port = 443},
      ntl::net::feature_set(ntl::net::network_feature::grpc), payload.size(),
      0, 1000);
  if (!inspection_request)
    return fail("make inspection request");

  async_offload_state async_context;
  ntl::net::offload::borrowed_callback_async_backend async_offload(
      inspection_service, &async_context,
      {.submit_borrowed = &submit_async_offload,
       .cancel = &cancel_async_offload,
       .stop = &stop_async_offload,
       .drain = &drain_async_offload});
  observed_offload_completion completion;
  if (!async_offload
           .submit_borrowed(*inspection_request, payload_view, {}, completion)
           .is_ok() ||
      !completion.completed ||
      !ntl::net::offload::validate(completion.response, *inspection_request)
           .is_ok() ||
      completion.response.verdict != ntl::net::inspection::verdict::block ||
      !async_offload.cancel(41).is_ok() || async_context.cancelled != 41)
    return fail("async inspection submit/complete/cancel");
  async_offload.stop();
  if (!async_offload.drain().is_ok() || !async_context.stopped ||
      !async_context.drained)
    return fail("async inspection stop/drain");

  auto inspection_backend = std::make_shared<
      ntl::net::offload::borrowed_callback_backend>(
          inspection_service,
          ntl::net::offload::callback{&execute_inspection, nullptr});
  const ntl::net::transform_context inspection_context_view{
      .network = {.kind = ntl::net::inspection::content_kind::tcp_message,
                  .flow_direction =
                      ntl::net::inspection::direction::inbound,
                  .flow_id = 9,
                  .source_port = 1234,
                  .destination_port = 443},
      .protocol_features =
          ntl::net::feature_set(ntl::net::network_feature::grpc)};
  const auto direct_inspection_request = ntl::net::offload::make_request(
      ntl::net::offload::operation::inspect_content, 1,
      inspection_context_view.network, inspection_context_view.protocol_features,
      payload.size(), 0, 1'000);
  ntl::net::offload::response_header direct_inspection_response{};
  const auto direct_inspection = direct_inspection_request
                                     ? inspection_backend->execute(
                                           *direct_inspection_request,
                                           payload_view, {},
                                           direct_inspection_response)
                                     : ntl::status{
                                           STATUS_INVALID_PARAMETER};
  if (!direct_inspection.is_ok()) {
    std::cerr << "direct inspection status=0x" << std::hex
              << static_cast<unsigned long>(direct_inspection) << std::dec
              << '\n';
    return fail("direct synchronous inspection");
  }
  ntl::net::offload::inspect_adapter inspection_adapter(
      inspection_backend, 1'000);
  inspection_backend.reset();
  ntl::net::borrowed_transform_pipeline inspection_pipeline;
  inspection_pipeline.decide(inspection_adapter.stage());
  const auto inspected = inspection_pipeline.run(
      inspection_context_view,
      ntl::net::inspection::content_view(payload));
  if (!inspected ||
      inspected->path != ntl::net::execution_path::offloaded ||
      inspected->verdict != ntl::net::inspection::verdict::block) {
    if (!inspected)
      std::cerr << "inspection pipeline status=0x" << std::hex
                << static_cast<unsigned long>(inspected.status()) << std::dec
                << '\n';
    else
      std::cerr << "inspection pipeline path="
                << static_cast<unsigned>(inspected->path)
                << " verdict=" << static_cast<unsigned>(inspected->verdict)
                << '\n';
    return fail("synchronous inspection pipeline");
  }

  const std::array<std::byte, 8> websocket_wire{
      std::byte{0x81}, std::byte{0x82}, std::byte{1}, std::byte{2},
      std::byte{3}, std::byte{4}, byte(static_cast<char>('H' ^ 1)),
      byte(static_cast<char>('i' ^ 2))};
  const auto websocket_view =
      ntl::net::scatter_view::from_contiguous(websocket_wire);
  const auto websocket_header = ntl::net::websocket::inspect_header(
      websocket_view, ntl::net::websocket::sender_role::client, {64, 0});
  std::array<std::byte, 2> websocket_payload{};
  const auto websocket = websocket_header
                             ? ntl::net::websocket::decode_payload_to(
                                   websocket_view, *websocket_header,
                                   websocket_payload)
                             : ntl::result<std::size_t>(
                                   ntl::unexpected(STATUS_DATA_ERROR));
  if (!websocket || *websocket != 2 || websocket_payload[0] != byte('H') ||
      websocket_payload[1] != byte('i'))
    return false;

  const auto datagram = ntl::net::http::encode_http3_datagram_to(
      output, 4, payload, {.maximum_payload_size = payload.size()});
  if (!datagram)
    return false;
  const auto parsed_datagram = ntl::net::http::http3_datagram_view::parse(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(output).first(*datagram)),
      {.maximum_payload_size = payload.size()});
  if (!parsed_datagram || parsed_datagram->request_stream_id() != 4 ||
      parsed_datagram->payload().size() != payload.size())
    return false;

  const std::array<std::byte, 15> qpack_wire{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x51}, std::byte{0x0b},
      byte('/'), byte('i'), byte('n'), byte('d'), byte('e'), byte('x'),
      byte('.'), byte('h'), byte('t'), byte('m'), byte('l')};
  std::array<std::byte, 64> qpack_scratch{};
  observed_qpack qpack_sink;
  const auto qpack = ntl::net::http3::decode_static_qpack(
      ntl::net::scatter_view::from_contiguous(qpack_wire), qpack_scratch,
      qpack_sink, qpack_scratch.size());
  if (!qpack || qpack->field_count != 1 ||
      qpack_sink.fields !=
          std::vector<std::pair<std::string, std::string>>{
              {":path", "/index.html"}})
    return false;

  std::vector<std::byte> extensions;
  constexpr std::string_view host = "EXAMPLE.TEST";
  append_u16(extensions, 0);
  append_u16(extensions, 2 + 1 + 2 + host.size());
  append_u16(extensions, 1 + 2 + host.size());
  extensions.push_back(std::byte{0});
  append_u16(extensions, host.size());
  for (const char value : host)
    extensions.push_back(byte(value));
  append_u16(extensions, 16);
  append_u16(extensions, 5);
  append_u16(extensions, 3);
  extensions.push_back(std::byte{2});
  extensions.push_back(byte('h'));
  extensions.push_back(byte('2'));
  append_u16(extensions, 0xfe0d);
  append_u16(extensions, 0);

  std::vector<std::byte> body;
  body.push_back(std::byte{3});
  body.push_back(std::byte{3});
  body.resize(body.size() + 32);
  body.push_back(std::byte{0});
  append_u16(body, 2);
  body.push_back(std::byte{0x13});
  body.push_back(std::byte{0x01});
  body.push_back(std::byte{1});
  body.push_back(std::byte{0});
  append_u16(body, extensions.size());
  body.insert(body.end(), extensions.begin(), extensions.end());

  std::vector<std::byte> client_hello;
  client_hello.push_back(std::byte{22});
  client_hello.push_back(std::byte{3});
  client_hello.push_back(std::byte{1});
  append_u16(client_hello, body.size() + 4);
  client_hello.push_back(std::byte{1});
  client_hello.push_back(
      static_cast<std::byte>((body.size() >> 16) & 0xffu));
  client_hello.push_back(
      static_cast<std::byte>((body.size() >> 8) & 0xffu));
  client_hello.push_back(static_cast<std::byte>(body.size() & 0xffu));
  client_hello.insert(client_hello.end(), body.begin(), body.end());

  std::array<std::byte, 1028> workspace{};
  observed_client_hello observer;
  const auto hello = ntl::net::inspect_tls_client_hello(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(client_hello)),
      workspace, observer,
      {.maximum_buffered_ciphertext = 2048,
       .maximum_client_hello = 1024,
       .receive_chunk_size = 64,
       .maximum_alpn_protocols = 4});
  if (!hello || observer.server_name != "example.test" ||
      observer.protocols != std::vector<std::string>{"h2"} ||
      !hello->encrypted_client_hello_extension_present)
    return false;
  auto invalid_compression = client_hello;
  invalid_compression[49] = std::byte{1};
  observed_client_hello invalid_observer;
  if (ntl::net::inspect_tls_client_hello(
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(invalid_compression)),
          workspace, invalid_observer,
          {.maximum_buffered_ciphertext = 2048,
           .maximum_client_hello = 1024,
           .receive_chunk_size = 64,
           .maximum_alpn_protocols = 4}))
    return false;

  const ntl::net::runtime_descriptor service{
      .domain = ntl::net::execution_domain::user,
      .path = ntl::net::execution_path::offloaded,
      .features = ntl::net::feature_set(
          ntl::net::network_feature::content_transform),
  };
  const auto request = ntl::net::offload::make_request(
      ntl::net::offload::operation::transform_content, 42,
      {.kind = ntl::net::inspection::content_kind::tcp_message,
       .flow_direction = ntl::net::inspection::direction::inbound,
       .flow_id = 9,
       .source_port = 50000,
       .destination_port = 443},
      ntl::net::feature_set(ntl::net::network_feature::grpc), 3, 64, 1000);
  if (!request || !ntl::net::offload::validate(*request, service).is_ok())
    return false;

  auto malformed = *request;
  malformed.protocol_features = {};
  if (ntl::net::offload::validate(malformed, service).is_ok())
    return false;
  malformed = *request;
  malformed.reserved = 1;
  if (ntl::net::offload::validate(malformed, service).is_ok())
    return false;
  ntl::net::offload::response_header pending{
      .kind = request->kind,
      .request_id = request->request_id,
      .completion_status = STATUS_PENDING,
  };
  if (ntl::net::offload::validate(pending, *request).is_ok())
    return false;
  auto invalid_verdict = pending;
  invalid_verdict.completion_status = STATUS_SUCCESS;
  invalid_verdict.verdict =
      static_cast<ntl::net::inspection::verdict>(0xff);
  return !ntl::net::offload::validate(invalid_verdict, *request).is_ok();
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
  ntl::net::http2::borrowed_connection_inspector inspector(
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
  ntl::net::http3::borrowed_stream_inspector inspector(decoder, 32);
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
  ntl::net::http3::borrowed_connection_inspector inspector(
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

  // One QUIC receive indication can contain several complete frames and can
  // therefore exceed the incremental assembly bound. The inspector must
  // drain complete frames while copying instead of rejecting the indication
  // solely because its aggregate size is larger than the workspace.
  std::vector<std::byte> coalesced{
      std::byte{0x01},
      static_cast<std::byte>(header_block.size())};
  coalesced.insert(coalesced.end(), header_block.begin(),
                   header_block.end());
  for (unsigned frame_index = 0; frame_index != 2; ++frame_index) {
    coalesced.push_back(std::byte{0x00});
    coalesced.push_back(std::byte{30});
    coalesced.insert(coalesced.end(), 30,
                     static_cast<std::byte>(0x40 + frame_index));
  }
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      coalesced_decoder;
  http3_sink coalesced_sink;
  ntl::net::http3::borrowed_connection_inspector coalesced_inspector(
      coalesced_decoder,
      {.maximum_concurrent_request_streams = 1,
       .maximum_buffered_bytes_per_stream = 40,
       .frames = {32}},
      4096);
  const auto coalesced_status =
      coalesced_inspector.consume_request_stream(
          4,
          ntl::net::scatter_view::from_contiguous(
              std::span<const std::byte>(coalesced)),
          true, coalesced_sink);

  const auto dynamic =
      hex_bytes("038110");
  const auto unsupported = decoder.decode(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(dynamic)),
      4096);
  constexpr ntl::net::quic::backend_capabilities replay{
      .available = true,
      .tls13_termination = true,
      .destination_redirection = false,
      .qpack_dynamic_table = false,
      .encrypted_client_hello = false,
      .arbitrary_browser_server_identity = false};
  constexpr ntl::net::quic::backend_capabilities browser{
      .available = true,
      .tls13_termination = true,
      .destination_redirection = true,
      .qpack_dynamic_table = true,
      .encrypted_client_hello = false,
      .arbitrary_browser_server_identity = true};
  return partial.is_ok() && complete.is_ok() &&
         sink.headers == 1 && sink.data_bytes == 3 &&
         coalesced_status.is_ok() && coalesced_sink.headers == 1 &&
         coalesced_sink.data_bytes == 60 &&
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
      decoder.underlying_ref().blocked_stream_count() != 1)
    return false;

  // Capacity=64, then Insert With Literal Name: x=y. Split the
  // encoder instruction to verify that fragmented control streams are
  // retained without exposing an incomplete entry.
  const auto encoder = hex_bytes("3f2141780179");
  if (encoder.size() != 6)
    return false;
  // Encoder instructions are an independent QUIC stream and may arrive
  // before the request stream. The decoder must retain that progress rather
  // than requiring a later encoder callback to wake the request.
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      encoder_first_decoder(qpack_limits);
  if (!encoder_first_decoder
           .consume_encoder_stream(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(encoder)))
           .is_ok())
    return false;
  const auto encoder_first = encoder_first_decoder.decode(
      8,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(header_block)),
      4096);
  if (!encoder_first || encoder_first->fields.size() != 1 ||
      encoder_first->fields[0].name != "x" ||
      encoder_first->fields[0].value != "y")
    return false;
  const auto first = decoder.consume_encoder_stream(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(encoder).first(3)));
  if (!first.is_ok() ||
      decoder.underlying_ref().insert_count() != 0)
    return false;
  const auto second = decoder.consume_encoder_stream(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(encoder).subspan(3)));
  if (!second.is_ok() ||
      decoder.underlying_ref().insert_count() != 1 ||
      decoder.underlying_ref().dynamic_table_entries() != 1)
    return false;

  const auto decoded = decoder.decode(
      4,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(header_block)),
      4096);
  if (!decoded || decoded->fields.size() != 1 ||
      decoded->fields[0].name != "x" ||
      decoded->fields[0].value != "y" ||
      decoder.underlying_ref().blocked_stream_count() != 0)
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
  ntl::net::http3::borrowed_connection_inspector inspector(
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
  auto transformer = [] {
    websocket::message_transform_pipeline owner(
        {1024, 4096, 4096, 4096, true});
    owner.transform([](websocket::message &message) {
      if (message.operation != websocket::opcode::text)
        return websocket::rewrite_result::unchanged();
      return websocket::rewrite_result::replace(
          std::vector<std::byte>{byte('B'), byte('y'), byte('e')});
    });
    return websocket::wire_transformer(
        websocket::sender_role::client, owner, std::nullopt,
        [] { return std::array<std::byte, 4>{
                   std::byte{5}, std::byte{6},
                   std::byte{7}, std::byte{8}}; });
  }();
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

bool test_websocket_stream_transform() {
  namespace websocket = ntl::net::websocket;
  websocket::message_transform_pipeline pipeline(
      {1024, 4096, 4096, 4096, true});
  pipeline.transform([](websocket::message &message) {
    if (message.operation != websocket::opcode::text)
      return websocket::rewrite_result::unchanged();
    return websocket::rewrite_result::replace(
        std::vector<std::byte>{byte('H'), byte('2')});
  });
  auto mask = [] {
    return std::array<std::byte, 4>{
        std::byte{5}, std::byte{6},
        std::byte{7}, std::byte{8}};
  };
  websocket::stream_transformer transformer(
      websocket::sender_role::client, pipeline,
      std::nullopt, mask, {1038, 4096});
  const std::array<std::byte, 8> input{
      std::byte{0x81}, std::byte{0x82},
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      byte(static_cast<char>('H' ^ 1)),
      byte(static_cast<char>('i' ^ 2))};
  const auto first = transformer.consume(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(input).first(3)));
  const auto second = transformer.consume(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(input).subspan(3)));
  if (!first || !first->wire.empty() ||
      transformer.pending_bytes() != 0 || !second ||
      second->complete_messages != 1 || second->wire.empty())
    return false;
  const auto output = ntl::net::scatter_view::from_contiguous(
      std::span<const std::byte>(second->wire));
  const auto header = websocket::inspect_header(
      output, websocket::sender_role::client, {4096, 0});
  const auto decoded = header
                           ? websocket::decode_payload(
                                 output, *header, 4096)
                           : ntl::result<std::vector<std::byte>>(
                                 ntl::unexpected(STATUS_DATA_ERROR));
  if (!decoded || *decoded !=
                      std::vector<std::byte>{byte('H'), byte('2')})
    return false;

  websocket::stream_transformer truncated(
      websocket::sender_role::client, pipeline,
      std::nullopt, mask, {1038, 4096});
  const auto rejected = truncated.consume(
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(input).first(3)), true);
  return !rejected && rejected.status() == STATUS_END_OF_FILE;
}

bool test_grpc_transform() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_encoders(encoders);

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

  auto transformer = [] {
    ntl::net::inspection::content_decoder_registry decoder_owner;
    ntl::net::inspection::register_standard_content_decoders(decoder_owner);
    ntl::net::inspection::content_encoder_registry encoder_owner;
    ntl::net::inspection::register_standard_content_encoders(encoder_owner);
    ntl::net::grpc::message_transform_pipeline policy_owner;
    policy_owner.transform(
        [](ntl::net::grpc::semantic_message &message) {
          message.payload.push_back(byte('!'));
          return ntl::net::grpc::transform_result::replace(
              std::move(message.payload));
        });
    return ntl::net::grpc::stream_transformer(
        ntl::net::grpc::direction::response, "gzip", policy_owner,
        decoder_owner, encoder_owner);
  }();
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
    : public ntl::net::quic::transport_backend {
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

  ntl::net::quic::backend_capabilities
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
  ntl::status run_borrowed(
      ntl::net::quic::backend_sink &) noexcept override {
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
  ntl::status reset_stream(
      std::uint64_t stream_id,
      std::uint64_t error_code) noexcept override {
    resets.push_back({stream_id, error_code, 0});
    return ntl::status::ok();
  }
  ntl::status write_qpack_decoder_stream(
      ntl::net::scatter_view plaintext) noexcept override {
    try {
      qpack_decoder.resize(plaintext.size());
      return qpack_decoder.empty()
                 ? ntl::status::ok()
                 : plaintext.copy_to(qpack_decoder);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }
  void stop() noexcept override {
    stopped = true;
    ++stop_calls;
  }
  ntl::status drain() noexcept override { return ntl::status::ok(); }

  std::vector<write_record> writes;
  std::vector<reset_record> resets;
  std::vector<std::byte> datagram;
  std::vector<std::byte> qpack_decoder;
  std::uint64_t next_bidirectional = 0;
  std::uint64_t next_unidirectional = 2;
  bool stopped = false;
  std::size_t stop_calls = 0;
};

class proxy_origin_fixture final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request) noexcept override {
    try {
      requests.push_back(request);
      ntl::net::http3::origin_response response;
      response.status = 200;
      response.headers.push_back({"content-type", "text/html"});
      constexpr std::string_view body = "origin";
      response.body.assign(
          reinterpret_cast<const std::byte *>(body.data()),
          reinterpret_cast<const std::byte *>(body.data() + body.size()));
      response.negotiated_protocol = "h3";
      response.trailers.push_back({"x-origin-trailer", "done"});
      return ntl::ok(std::move(response));
    } catch (...) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    }
  }

  std::vector<ntl::net::http3::origin_request> requests;
};

class queued_proxy_origin_fixture final
    : public ntl::net::http3::async_origin_transport {
public:
  struct pending_exchange {
    std::uint64_t exchange_id = 0;
    ntl::net::http3::origin_request request;
    ntl::net::http3::origin_completion completion;
    bool cancelled = false;
  };

  ntl::status submit(
      std::uint64_t exchange_id,
      ntl::net::http3::origin_request request,
      ntl::net::http3::origin_completion completion) noexcept override {
    if (!completion)
      return STATUS_INVALID_PARAMETER;
    try {
      pending.push_back(
          {exchange_id, std::move(request), std::move(completion), false});
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  void cancel(std::uint64_t exchange_id) noexcept override {
    for (auto &exchange : pending) {
      if (exchange.exchange_id == exchange_id)
        exchange.cancelled = true;
    }
    try {
      cancelled.push_back(exchange_id);
    } catch (...) {
    }
  }

  void complete(std::size_t index) {
    ntl::net::http3::origin_response response;
    response.status = 200;
    response.headers.push_back({"content-type", "text/plain"});
    response.body = {byte('o'), byte('k')};
    response.negotiated_protocol = "h3";
    pending.at(index).completion(ntl::ok(std::move(response)));
  }

  std::vector<pending_exchange> pending;
  std::vector<std::uint64_t> cancelled;
};

class async_proxy_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_exchange_complete(
      std::uint64_t stream_id,
      const ntl::net::http::request_message &,
      const ntl::net::http::response_message &,
      bool) noexcept override {
    try {
      completion_order.push_back(stream_id);
    } catch (...) {
    }
  }

  std::vector<std::uint64_t> completion_order;
};

class proxy_observer_fixture final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    ++stages;
    metadata_valid = metadata_valid &&
                     context.wire_protocol() ==
                         ntl::net::http::protocol::http3 &&
                     context.stream_id() == context.exchange_id() &&
                     context.connection().process_id &&
                     *context.connection().process_id == 42 &&
                     context.connection().application_label &&
                     *context.connection().application_label ==
                         "browser.exe" &&
                     context.tls().server_name &&
                     *context.tls().server_name == "example.test" &&
                     context.tls().alpn && *context.tls().alpn == "h3" &&
                      context.method() == "POST" &&
                      context.path() == "/inspect" &&
                      context.query() == "mode=deep";
    if (context.direction() ==
            ntl::net::http::message_direction::request &&
        context.stage() == ntl::net::http::inspection_stage::headers)
      request_headers_saw_transform =
          context.headers().first("x-transformed") == "yes";
    if (context.direction() ==
        ntl::net::http::message_direction::response)
      response_associated = context.response() != nullptr &&
                            context.request().path ==
                                "/inspect?mode=deep";
  }

  std::size_t stages = 0;
  bool metadata_valid = true;
  bool response_associated = false;
  bool request_headers_saw_transform = false;
};

std::vector<std::byte> make_http3_request_wire(
    std::span<const ntl::net::http3::header_field> fields,
    std::span<const std::byte> body = {}) {
  ntl::net::http3::bounded_static_qpack_encoder encoder;
  auto block = encoder.encode(fields, 64 * 1024);
  if (!block)
    return {};
  std::vector<std::byte> wire;
  if (!ntl::net::http3::webtransport::session_detail::append_frame(
           wire,
           static_cast<std::uint64_t>(
               ntl::net::http3::frame_type::headers),
           *block)
           .is_ok())
    return {};
  if (!body.empty() &&
      !ntl::net::http3::webtransport::session_detail::append_frame(
           wire,
           static_cast<std::uint64_t>(
               ntl::net::http3::frame_type::data),
           body)
           .is_ok())
    return {};
  return wire;
}

bool test_http3_proxy_connection() {
  static_assert(
      ntl::net::http3::proxy_connection::serializes_callbacks);
  static_assert(
      ntl::net::http3::proxy_connection::asynchronous_origin_transport);
  using ntl::net::inspection::verdict;
  auto backend_owner = std::make_shared<recording_quic_backend>();
  auto &backend = *backend_owner;
  auto origin_owner = std::make_shared<proxy_origin_fixture>();
  auto &origin = *origin_owner;
  auto async_origin = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin_owner);
  auto observer_owner = std::make_shared<proxy_observer_fixture>();
  auto &observer = *observer_owner;
  auto decoders_owner = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders_owner = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy_owner = std::make_shared<ntl::net::http::inspection_policy>();
  policy_owner->use_content_codecs(decoders_owner, encoders_owner);
  auto &policy = *policy_owner;
  policy.requests()
      .at_headers()
      .when([](const ntl::net::http::inspection_context_view &context) {
        const auto policy_header = context.headers().first("x-policy");
        return context.method() == "POST" &&
               context.path() == "/inspect" &&
               context.query() == "mode=deep" && policy_header &&
               *policy_header == "allow" &&
               context.connection().application_label &&
               *context.connection().application_label == "browser.exe";
      })
      .decide([](const ntl::net::http::inspection_context_view &) {
        return verdict::permit;
      });
  policy.requests()
      .at_headers()
      .when([](const ntl::net::http::inspection_context_view &context) {
        return context.path() == "/blocked";
      })
      .decide([](const ntl::net::http::inspection_context_view &) {
        return verdict::block;
      });
  policy.requests()
      .at_body_chunk()
      .decide([](const ntl::net::http::inspection_context_view &context) {
        constexpr std::string_view expected = "hello";
        return context.body_chunk().size() == expected.size()
                   ? verdict::permit
                   : verdict::block;
      });
  policy.transforms_ref().requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-transformed", "yes");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  policy.transforms_ref().responses().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &) {
        constexpr std::string_view rewritten = "rewritten";
        return ntl::net::http::rewrite_result::replace_body(
            std::vector<std::byte>(
                reinterpret_cast<const std::byte *>(rewritten.data()),
                reinterpret_cast<const std::byte *>(
                    rewritten.data() + rewritten.size())),
            ntl::net::http::transformed_body_coding::identity);
      });

  const ntl::net::http::inspection_session_metadata metadata{
      .connection = {.flow_id = 7,
                     .process_id = 42,
                     .application_label = "browser.exe"},
      .tls = {.server_name = "example.test",
              .alpn = "h3"}};
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, async_origin, policy_owner, metadata, observer_owner,
      nullptr, nullptr,
      {.maximum_concurrent_request_streams = 8,
       .maximum_buffered_bytes_per_stream = 64 * 1024,
       .maximum_aggregate_body_bytes = 128 * 1024,
       .maximum_frame_payload = 64 * 1024,
       .maximum_decoded_header_bytes = 64 * 1024,
       .maximum_control_stream_bytes = 4096,
       .maximum_extension_stream_bytes = 64 * 1024,
       .maximum_capsule_wire_bytes = 64 * 1024,
       .maximum_blocked_streams = 4,
       .qpack_table_capacity = 256,
       .require_http3_origin = true,
       .enable_webtransport = true});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  proxy.on_datagram_send_state(true, 1200);
  proxy.on_reliable_reset_negotiated(true);
  if (!proxy.on_connected("h3").is_ok() || backend.writes.size() != 1)
    return false;

  constexpr std::string_view request_body = "hello";
  const std::vector<ntl::net::http3::header_field> request_headers{
      {":method", "POST", false},
      {":scheme", "https", false},
      {":authority", "example.test", false},
      {":path", "/inspect?mode=deep", false},
      {"content-length", "5", false},
      {"x-policy", "allow", false}};
  const auto wire = make_http3_request_wire(
      request_headers, std::as_bytes(std::span(request_body)));
  if (wire.empty())
    return false;
  const std::size_t split = wire.size() / 2;
  if (!proxy
           .on_request_stream(
               0,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(wire).first(split)),
               false)
           .is_ok() ||
      !proxy
           .on_request_stream(
               0,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(wire).subspan(split)),
               true)
           .is_ok())
    return false;
  if (origin.requests.size() != 1 ||
      origin.requests[0].method != "POST" ||
      origin.requests[0].path != "/inspect?mode=deep" ||
      origin.requests[0].body.size() != request_body.size() ||
      origin.requests[0].trailers.size() != 0 ||
      observer.stages != 6 || !observer.metadata_valid ||
      !observer.response_associated ||
      !observer.request_headers_saw_transform ||
      proxy.active_requests() != 0 ||
      proxy.buffered_body_bytes() != 0)
    return false;
  bool transformed_header = false;
  for (const auto &field : origin.requests[0].headers)
    transformed_header = transformed_header ||
                         (field.name == "x-transformed" &&
                          field.value == "yes");
  if (!transformed_header || backend.writes.size() < 3 ||
      !backend.writes.back().final)
    return false;

  const std::vector<ntl::net::http3::header_field> blocked_headers{
      {":method", "GET", false},
      {":scheme", "https", false},
      {":authority", "example.test", false},
      {":path", "/blocked", false}};
  const auto blocked = make_http3_request_wire(blocked_headers);
  if (blocked.empty() ||
      !proxy
           .on_request_stream(
               4,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(blocked)),
               true)
           .is_ok() ||
      origin.requests.size() != 1 || !backend.writes.back().final)
    return false;

  // A dynamically indexed but semantically invalid request first blocks,
  // then resumes after encoder instructions. The adapter emits decoder
  // acknowledgements and resets only that stream.
  const auto dynamic_block = hex_bytes("020080");
  std::vector<std::byte> dynamic_wire{std::byte{0x01},
                                      std::byte{0x03}};
  dynamic_wire.insert(
      dynamic_wire.end(), dynamic_block.begin(), dynamic_block.end());
  if (!proxy
           .on_request_stream(
               8,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(dynamic_wire)),
               true)
           .is_ok())
    return false;
  const auto encoder_instructions = hex_bytes("3f2141780179");
  if (!proxy
           .on_qpack_encoder_stream(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(encoder_instructions)))
           .is_ok() ||
      backend.qpack_decoder.empty() || backend.resets.empty() ||
      backend.resets.back().stream_id != 8)
    return false;

  if (!proxy.begin_drain(8).is_ok() || !proxy.draining())
    return false;
  proxy.stop();
  return backend.stopped;
}

bool test_http3_proxy_async_origin() {
  auto backend_owner = std::make_shared<recording_quic_backend>();
  auto &backend = *backend_owner;
  auto origin_owner = std::make_shared<queued_proxy_origin_fixture>();
  auto &origin = *origin_owner;
  auto observer_owner = std::make_shared<async_proxy_observer>();
  auto &observer = *observer_owner;
  auto decoders_owner = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders_owner = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy_owner = std::make_shared<ntl::net::http::inspection_policy>();
  policy_owner->use_content_codecs(decoders_owner, encoders_owner);
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, origin_owner, policy_owner,
      {.tls = {.server_name = "async.test", .alpn = "h3"}},
      observer_owner, nullptr, nullptr,
      {.maximum_concurrent_request_streams = 8,
       .maximum_buffered_bytes_per_stream = 4096,
       .maximum_aggregate_body_bytes = 16 * 1024,
       .maximum_frame_payload = 4096,
       .maximum_decoded_header_bytes = 4096,
       .maximum_control_stream_bytes = 1024,
       .maximum_extension_stream_bytes = 4096,
       .maximum_concurrent_extension_streams = 8,
       .maximum_aggregate_extension_stream_bytes = 16 * 1024,
       .maximum_capsule_wire_bytes = 4096,
       .maximum_blocked_streams = 4,
       .maximum_concurrent_webtransport_sessions = 2,
       .qpack_table_capacity = 256});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  if (!proxy.on_connected("h3").is_ok())
    return false;

  const auto submit = [&](std::uint64_t stream_id,
                          std::string path) {
    const std::vector<ntl::net::http3::header_field> headers{
        {":method", "GET", false},
        {":scheme", "https", false},
        {":authority", "async.test", false},
        {":path", std::move(path), false}};
    const auto wire = make_http3_request_wire(headers);
    return !wire.empty() &&
           proxy
               .on_request_stream(
                   stream_id,
                   ntl::net::scatter_view::from_contiguous(
                       std::span<const std::byte>(wire)),
                   true)
               .is_ok();
  };

  const std::size_t writes_before_origins = backend.writes.size();
  if (!submit(0, "/slow") || !submit(4, "/fast") ||
      origin.pending.size() != 2 || proxy.active_requests() != 2 ||
      backend.writes.size() != writes_before_origins)
    return false;

  origin.complete(1);
  if (observer.completion_order != std::vector<std::uint64_t>{4} ||
      proxy.active_requests() != 1)
    return false;
  origin.complete(0);
  if (observer.completion_order !=
          std::vector<std::uint64_t>{4, 0} ||
      proxy.active_requests() != 0)
    return false;

  const std::size_t writes_after_once = backend.writes.size();
  origin.complete(0);
  if (backend.writes.size() != writes_after_once ||
      observer.completion_order !=
          std::vector<std::uint64_t>{4, 0})
    return false;

  if (!submit(8, "/reset") || origin.pending.size() != 3 ||
      !proxy.on_peer_send_aborted(8, 0).is_ok() ||
      proxy.active_requests() != 0 || !origin.pending[2].cancelled)
    return false;
  const std::size_t writes_after_reset = backend.writes.size();
  origin.complete(2);
  if (backend.writes.size() != writes_after_reset ||
      observer.completion_order !=
          std::vector<std::uint64_t>{4, 0})
    return false;

  if (!submit(12, "/drain") || origin.pending.size() != 4 ||
      !proxy.begin_drain(8).is_ok() ||
      proxy.active_requests() != 0 || !origin.pending[3].cancelled)
    return false;
  const std::size_t writes_after_drain = backend.writes.size();
  // The origin owns only a weak completion. Destroying the proxy facade while
  // it retains a cancelled exchange must make the late completion a harmless
  // no-op instead of dereferencing a raw proxy pointer.
  std::weak_ptr<ntl::net::http3::proxy_connection> lifetime = *proxy_owner;
  auto last_proxy_owner = std::move(proxy_owner).value();
  last_proxy_owner.reset();
  if (!lifetime.expired() || !origin.pending[3].cancelled)
    return false;
  origin.complete(3);
  return backend.writes.size() == writes_after_drain &&
         observer.completion_order ==
             std::vector<std::uint64_t>{4, 0} &&
         origin.cancelled == std::vector<std::uint64_t>{8, 12};
}

class webtransport_proxy_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_webtransport_payload(
      const ntl::net::http3::webtransport::payload &value) noexcept override {
    ++payloads;
    last_session = value.session_id;
  }
  void on_webtransport_reset(
      std::uint64_t session_id,
      std::uint32_t application_error) noexcept override {
    ++resets;
    last_session = session_id;
    last_error = application_error;
  }

  std::size_t payloads = 0;
  std::size_t resets = 0;
  std::uint64_t last_session = 0;
  std::uint32_t last_error = 0;
};

bool test_http3_proxy_webtransport() {
  namespace wt = ntl::net::http3::webtransport;
  const auto check = [](bool condition, int line) {
    if (!condition)
      std::cerr << "HTTP/3 proxy WebTransport contract failed at "
                << line << '\n';
    return condition;
  };
  auto backend_owner = std::make_shared<recording_quic_backend>();
  auto origin_owner = std::make_shared<proxy_origin_fixture>();
  auto &origin = *origin_owner;
  auto async_origin = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin_owner);
  auto observer_owner = std::make_shared<webtransport_proxy_observer>();
  auto &observer = *observer_owner;
  auto decoders_owner = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders_owner = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy_owner = std::make_shared<ntl::net::http::inspection_policy>();
  policy_owner->use_content_codecs(decoders_owner, encoders_owner);
  auto &policy = *policy_owner;
  std::vector<ntl::net::http::inspection_stage> connect_stages;
  bool connect_headers_saw_transform = false;
  policy.transforms_ref().requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-wt-transformed", "yes");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  policy.requests().at_headers().decide(
      [&](const ntl::net::http::inspection_context_view &context) {
        connect_stages.push_back(context.stage());
        connect_headers_saw_transform =
            context.headers().first("x-wt-transformed") == "yes";
        return ntl::net::inspection::verdict::permit;
      });
  policy.requests().at_message_complete().decide(
      [&](const ntl::net::http::inspection_context_view &context) {
        connect_stages.push_back(context.stage());
        return ntl::net::inspection::verdict::permit;
      });
  auto payload_policy_owner = std::make_shared<wt::transform_session>(
      wt::transform_limits{
          .session = {.maximum_datagrams = 1}});
  auto &payload_policy = *payload_policy_owner;
  payload_policy.transform([](wt::payload &payload) {
    if (payload.kind == wt::payload_kind::capsule)
      return wt::transform_result::unchanged();
    auto bytes = payload.bytes;
    bytes.push_back(byte('!'));
    return wt::transform_result::replace(std::move(bytes));
  });
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, async_origin, policy_owner,
      {.connection = {.process_id = 84,
                      .application_label = "webtransport-client.exe"},
       .tls = {.server_name = "wt.test", .alpn = "h3"}},
      observer_owner, payload_policy_owner, nullptr,
      {.maximum_concurrent_request_streams = 8,
       .maximum_buffered_bytes_per_stream = 64 * 1024,
       .maximum_aggregate_body_bytes = 128 * 1024,
       .maximum_frame_payload = 64 * 1024,
       .maximum_decoded_header_bytes = 64 * 1024,
       .maximum_control_stream_bytes = 4096,
       .maximum_extension_stream_bytes = 64 * 1024,
       .maximum_capsule_wire_bytes = 64 * 1024,
       .maximum_blocked_streams = 4,
       .qpack_table_capacity = 256,
       .require_http3_origin = true,
       .enable_webtransport = true});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  proxy.on_datagram_send_state(true, 1200);
  proxy.on_reliable_reset_negotiated(true);
  if (!check(proxy.on_connected("h3").is_ok(), __LINE__))
    return false;
  auto peer_control = wt::encode_control_stream(false, {}, 4096);
  if (!check(peer_control &&
      proxy
           .on_peer_unidirectional_stream(
               6,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(*peer_control)),
               false)
           .is_ok() &&
      proxy.peer_settings().client_ready(), __LINE__))
    return false;

  const std::vector<ntl::net::http3::header_field> connect_headers{
      {":method", "CONNECT", false},
      {":protocol", "webtransport-h3", false},
      {":scheme", "https", false},
      {":authority", "wt.test", false},
      {":path", "/session", false},
      {"origin", "https://wt.test", false}};
  const auto connect = make_http3_request_wire(connect_headers);
  const auto connect_status = connect.empty()
      ? ntl::status{STATUS_DATA_ERROR}
      : proxy.on_request_stream(
            0,
            ntl::net::scatter_view::from_contiguous(
                std::span<const std::byte>(connect)),
             false);
  const auto primary_session = proxy.webtransport_session(0);
  if (!check(connect_status.is_ok() &&
      primary_session && primary_session->active() &&
      primary_session->session_id() == 0 &&
      connect_headers_saw_transform &&
      connect_stages ==
          std::vector<ntl::net::http::inspection_stage>{
              ntl::net::http::inspection_stage::headers,
              ntl::net::http::inspection_stage::message_complete},
      __LINE__)) {
    std::cerr << "connect status="
              << static_cast<unsigned>(static_cast<NTSTATUS>(connect_status))
              << " active=" << (primary_session && primary_session->active())
              << " id=" << (primary_session ? primary_session->session_id() : 0)
              << '\n';
    return false;
  }

  const auto second_connect_status = proxy.on_request_stream(
      12,
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(connect)),
      false);
  const auto second_session = proxy.webtransport_session(12);
  if (!check(second_connect_status.is_ok() && second_session &&
             second_session->active() &&
             proxy.webtransport_session_count() == 2,
             __LINE__))
    return false;

  constexpr std::string_view text = "wt";
  auto datagram = ntl::net::http::encode_http3_datagram(
      0, std::as_bytes(std::span(text)), {64 * 1024});
  if (!check(datagram &&
      proxy
           .on_datagram(
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(*datagram)))
           .is_ok(), __LINE__))
    return false;
  auto second_datagram = ntl::net::http::encode_http3_datagram(
      12, std::as_bytes(std::span(text)), {64 * 1024});
  if (!check(second_datagram &&
                 proxy
                     .on_datagram(
                         ntl::net::scatter_view::from_contiguous(
                             std::span<const std::byte>(*second_datagram)))
                     .is_ok(),
             __LINE__))
    return false;

  std::vector<std::byte> bidirectional;
  if (!ntl::net::http3::append_quic_varint(
           bidirectional, wt::bidirectional_stream_signal)
           .is_ok() ||
      !ntl::net::http3::append_quic_varint(bidirectional, 0).is_ok())
    return false;
  bidirectional.insert(
      bidirectional.end(), std::as_bytes(std::span(text)).begin(),
      std::as_bytes(std::span(text)).end());
  if (!check(proxy
           .on_peer_bidirectional_stream(
               4,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(bidirectional)),
               true)
           .is_ok(), __LINE__))
    return false;

  std::vector<std::byte> unidirectional;
  if (!ntl::net::http3::append_quic_varint(
           unidirectional, wt::unidirectional_stream_type)
           .is_ok() ||
      !ntl::net::http3::append_quic_varint(unidirectional, 0).is_ok())
    return false;
  unidirectional.insert(
      unidirectional.end(), std::as_bytes(std::span(text)).begin(),
      std::as_bytes(std::span(text)).end());
  if (!check(proxy
           .on_peer_unidirectional_stream(
               10,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(unidirectional)),
               true)
           .is_ok(), __LINE__))
    return false;

  auto capsule = ntl::net::http::encode_capsule(
      wt::wt_drain_session, {}, {.maximum_payload_size = 64 * 1024});
  std::vector<std::byte> capsule_frame;
  if (!check(capsule &&
      wt::session_detail::append_frame(
           capsule_frame,
           static_cast<std::uint64_t>(
               ntl::net::http3::frame_type::data),
           *capsule)
           .is_ok() &&
      proxy
           .on_request_stream(
               0,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(capsule_frame)),
               false)
           .is_ok(), __LINE__))
    return false;

  std::vector<std::byte> reset_prefix;
  if (!check(ntl::net::http3::append_quic_varint(
           reset_prefix, wt::bidirectional_stream_signal)
           .is_ok() &&
      ntl::net::http3::append_quic_varint(reset_prefix, 0).is_ok() &&
      proxy
           .on_peer_bidirectional_stream(
               8,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(reset_prefix)),
               false)
           .is_ok() &&
      proxy
           .on_peer_send_aborted(
               8, wt::application_error_to_http3(0x1234))
           .is_ok(), __LINE__))
    return false;

  if (!check(proxy.on_request_stream(12, {}, true).is_ok() &&
                 proxy.on_request_stream(0, {}, true).is_ok(),
             __LINE__))
    return false;
  return check(observer.payloads == 5 && observer.resets == 1 &&
         observer.last_session == 0 && observer.last_error == 0x1234 &&
         proxy.active_requests() == 0 &&
         proxy.webtransport_session_count() == 0 &&
         connect_stages ==
             std::vector<ntl::net::http::inspection_stage>{
                 ntl::net::http::inspection_stage::headers,
                 ntl::net::http::inspection_stage::message_complete,
                 ntl::net::http::inspection_stage::headers,
                 ntl::net::http::inspection_stage::message_complete} &&
         origin.requests.empty(), __LINE__);
}

bool test_http3_proxy_extension_bounds() {
  auto backend_owner = std::make_shared<recording_quic_backend>();
  auto origin_owner = std::make_shared<proxy_origin_fixture>();
  auto async_origin = std::make_shared<
      ntl::net::http3::immediate_origin_transport_adapter>(origin_owner);
  auto decoders_owner = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders_owner = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy_owner = std::make_shared<ntl::net::http::inspection_policy>();
  policy_owner->use_content_codecs(decoders_owner, encoders_owner);
  auto proxy_owner = ntl::net::http3::proxy_connection::create(
      backend_owner, async_origin, policy_owner, {}, nullptr, nullptr,
      nullptr,
      {.maximum_concurrent_request_streams = 2,
       .maximum_buffered_bytes_per_stream = 1024,
       .maximum_aggregate_body_bytes = 2048,
       .maximum_frame_payload = 1024,
       .maximum_decoded_header_bytes = 1024,
       .maximum_control_stream_bytes = 64,
       .maximum_extension_stream_bytes = 2,
       .maximum_concurrent_extension_streams = 2,
       .maximum_aggregate_extension_stream_bytes = 2,
       .maximum_capsule_wire_bytes = 64,
       .maximum_blocked_streams = 1,
       .maximum_concurrent_webtransport_sessions = 1,
       .qpack_table_capacity = 64});
  if (!proxy_owner)
    return false;
  auto &proxy = **proxy_owner;
  if (!proxy.on_connected("h3").is_ok())
    return false;

  const std::array<std::byte, 1> incomplete{std::byte{0x40}};
  const auto incomplete_view =
      ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(incomplete));
  if (!proxy.on_peer_unidirectional_stream(6, incomplete_view, false).is_ok() ||
      !proxy.on_peer_bidirectional_stream(8, incomplete_view, false).is_ok() ||
      proxy.active_extension_streams() != 2 ||
      proxy.buffered_extension_bytes() != 2 ||
      proxy.on_peer_unidirectional_stream(10, incomplete_view, false) !=
          STATUS_QUOTA_EXCEEDED)
    return false;

  if (!proxy.on_peer_send_aborted(6, 0).is_ok() ||
      proxy.active_extension_streams() != 1 ||
      proxy.buffered_extension_bytes() != 1)
    return false;

  const std::array<std::byte, 1> unknown_final{std::byte{0x21}};
  if (!proxy
           .on_peer_unidirectional_stream(
               10,
               ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(unknown_final)),
               true)
           .is_ok() ||
      proxy.active_extension_streams() != 1 ||
      proxy.buffered_extension_bytes() != 1)
    return false;

  const std::array<std::byte, 2> overflow{
      std::byte{0}, std::byte{0}};
  return proxy.on_peer_bidirectional_stream(
             8,
             ntl::net::scatter_view::from_contiguous(
                 std::span<const std::byte>(overflow)),
             true) == STATUS_QUOTA_EXCEEDED &&
         proxy.active_extension_streams() == 0 &&
         proxy.buffered_extension_bytes() == 0;
}

class close_on_connect_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_connected(std::string_view) noexcept override {
    if (const auto owner = proxy.lock())
      owner->close();
  }

  std::weak_ptr<ntl::net::http3::proxy_connection> proxy;
};

bool test_http3_proxy_lifetime() {
  const auto make_proxy = [](
      std::shared_ptr<recording_quic_backend> backend,
      std::shared_ptr<ntl::net::http3::proxy_connection_observer> observer = {})
      -> ntl::result<std::shared_ptr<ntl::net::http3::proxy_connection>> {
    auto origin = std::make_shared<proxy_origin_fixture>();
    auto async_origin = std::make_shared<
        ntl::net::http3::immediate_origin_transport_adapter>(origin);
    return ntl::net::http3::proxy_connection::create(
        std::move(backend), std::move(async_origin),
        std::make_shared<ntl::net::http::inspection_policy>(),
        {}, std::move(observer));
  };

  auto backend = std::make_shared<recording_quic_backend>();
  auto observer = std::make_shared<close_on_connect_observer>();
  auto created = make_proxy(backend, observer);
  if (!created)
    return false;
  auto proxy = std::move(created).value();
  observer->proxy = proxy;
  if (proxy->on_connected("h3") != STATUS_DELETE_PENDING ||
      !proxy->closed() || backend->stop_calls != 1)
    return false;
  proxy->close();
  proxy->stop();
  if (backend->stop_calls != 1 ||
      proxy->on_request_stream(0, {}, true) != STATUS_DELETE_PENDING ||
      proxy->run() != STATUS_DELETE_PENDING)
    return false;

  auto released_backend = std::make_shared<recording_quic_backend>();
  auto released_proxy = make_proxy(released_backend);
  if (!released_proxy)
    return false;
  released_backend.reset();
  if ((*released_proxy)->run() != STATUS_DELETE_PENDING)
    return false;
  (*released_proxy)->close();
  (*released_proxy)->close();
  return (*released_proxy)->closed() && (*released_proxy)->drain().is_ok();
}

bool test_webtransport_backend_session() {
  namespace wt = ntl::net::http3::webtransport;
  auto backend_owner = std::make_shared<recording_quic_backend>();
  auto &backend = *backend_owner;
  wt::backend_session client(backend_owner);
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
      client.active() || !client.client_response_pending() ||
      client.session_id() != 0 ||
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
  if (client.accept_client_response(0, 199) != STATUS_INVALID_PARAMETER ||
      !client.accept_client_response(0, 200).is_ok() || !client.active() ||
      client.client_response_pending())
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

  auto server_backend_owner = std::make_shared<recording_quic_backend>();
  auto &server_backend = *server_backend_owner;
  wt::backend_session server(server_backend_owner);
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
  if (!response_headers || response_headers->fields.size() != 1 ||
      response_headers->fields[0].name != ":status" ||
      response_headers->fields[0].value != "200")
    return false;

  // Rejecting Extended CONNECT is a header-stage policy action, not session
  // establishment.  It must work even when WebTransport transport features
  // have not finished negotiating.
  auto reject_backend_owner = std::make_shared<recording_quic_backend>();
  auto &reject_backend = *reject_backend_owner;
  wt::backend_session reject(reject_backend_owner);
  if (reject.reject_server(4, 200) != STATUS_INVALID_PARAMETER ||
      reject.reject_server(4, 600) != STATUS_INVALID_PARAMETER ||
      !reject.reject_server(4, 403).is_ok() || reject.active() ||
      reject_backend.writes.size() != 1 ||
      !reject_backend.writes[0].final)
    return false;
  const auto reject_frame = ntl::net::http3::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(
          reject_backend.writes[0].bytes));
  if (!reject_frame)
    return false;
  auto reject_headers = decoder.decode(4, reject_frame->payload(), 4096);
  if (!reject_headers || reject_headers->fields.size() != 1 ||
      reject_headers->fields[0].name != ":status" ||
      reject_headers->fields[0].value != "403")
    return false;

  auto rejected_client_backend_owner =
      std::make_shared<recording_quic_backend>();
  wt::backend_session rejected_client(rejected_client_backend_owner);
  rejected_client.set_negotiated_transport({true, true});
  if (!rejected_client.open_client({.authority = "blocked.test:443",
                                    .path = "/transport",
                                    .origin = "https://blocked.test"})
           .is_ok() ||
      rejected_client.active() ||
      !rejected_client.client_response_pending() ||
      rejected_client.reject_client_response(0, 200) !=
          STATUS_INVALID_PARAMETER ||
      !rejected_client.reject_client_response(0, 403).is_ok() ||
      rejected_client.active() || rejected_client.client_response_pending() ||
      rejected_client.session_id() != 0)
    return false;
  if (rejected_client.send_datagram({}) != STATUS_INVALID_DEVICE_STATE)
    return false;

  auto released_backend = std::make_shared<recording_quic_backend>();
  wt::backend_session released(released_backend);
  released_backend.reset();
  return released.send_local_settings(false) == STATUS_DELETE_PENDING;
}

bool test_product_inspection_policy() {
  using namespace ntl::net::inspection;
  product_inspection_policy policy;
  tls_inspection_observation_view observation;
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
  ntl::net::inspection::tls_inspection_observation_view observation;
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
  if (!test_dual_runtime_core())
    return 16;
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
  if (!test_websocket_stream_transform())
    return 12;
  if (!test_grpc_transform())
    return 13;
  if (!test_webtransport_transform())
    return 14;
  if (!test_product_inspection_policy())
    return 15;
  if (!test_webtransport_backend_session())
    return 16;
  if (!test_http3_proxy_connection())
    return 17;
  if (!test_http3_proxy_webtransport())
    return 18;
  if (!test_http3_proxy_extension_bounds())
    return 19;
  if (!test_http3_proxy_async_origin())
    return 20;
  if (!test_http3_proxy_lifetime())
    return 21;
  std::cout
      << "NTL protocol adapters ok: websocket, http2, http3, "
         "static/dynamic-qpack, blocked-stream-resume, "
         "datagram, capsule, extended-connect, "
         "webtransport-draft16, quic-backend, permessage-deflate, "
         "gzip, deflate, br, "
         "content-decoder, tls-policy, websocket-transform/stream, "
         "grpc-transform, webtransport-transform, webtransport-session, "
         "http3-proxy-connection/webtransport/bounded-extension-streams/"
         "async-origin/lifetime, "
         "product-policy\n";
  return 0;
}
