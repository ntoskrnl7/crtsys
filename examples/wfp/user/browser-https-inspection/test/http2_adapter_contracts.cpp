#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ntl/net/http/transform>
#include <ntl/net/http2/framing>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/io/async_socket>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/stream>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/stream_transform>
#include <ntl/net/websocket/transform>

#include "browser_log.hpp"
#include "browser_proxy.hpp"
#include "eager_test_task.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

namespace sample = crtsys::wfp_sample;
namespace browser = crtsys::wfp_sample::browser_https;

namespace {

constexpr std::wstring_view server_name = L"adapter.example.test";
constexpr std::string_view server_name_ascii = "adapter.example.test";
constexpr std::string_view client_preface =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr std::size_t maximum_message = 2 * 1024 * 1024;

std::atomic<unsigned> proxy_progress{0};
std::atomic<unsigned> origin_progress{0};
std::atomic<unsigned> client_progress{0};

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

std::vector<std::byte> bytes_of(std::string_view value) {
  const auto bytes = std::as_bytes(std::span(value));
  return {bytes.begin(), bytes.end()};
}

std::string text_of(std::span<const std::byte> value) {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

sample::socket_owner connect_loopback(std::uint16_t port) {
  sample::socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    sample::throw_socket("WSASocketW(HTTP/2 adapter)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    sample::throw_socket("connect(HTTP/2 adapter)");
  return socket;
}

class temporary_directory {
public:
  temporary_directory() {
    root_ = std::filesystem::temp_directory_path() /
            (L"crtsys-http2-adapter-" +
             std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(::GetTickCount64()));
    std::filesystem::create_directories(root_);
  }
  temporary_directory(const temporary_directory &) = delete;
  temporary_directory &operator=(const temporary_directory &) = delete;
  ~temporary_directory() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }
  const std::filesystem::path &get() const noexcept { return root_; }

private:
  std::filesystem::path root_;
};

sample::coroutine_task<unsigned> read_exactly(
    ntl::net::tls_stream &stream, std::span<std::byte> destination) {
  std::size_t offset = 0;
  while (offset != destination.size()) {
    const std::size_t count =
        co_await stream.read_some_borrowed(destination.subspan(offset));
    if (count == 0)
      throw std::runtime_error("HTTP/2 TLS stream ended early");
    offset += count;
  }
  co_return 0;
}

struct wire_frame {
  std::vector<std::byte> wire;
  ntl::net::http2::frame_header header;
};

sample::coroutine_task<wire_frame> read_frame(ntl::net::tls_stream &stream) {
  std::array<std::byte, ntl::net::http2::frame_header_size> header_bytes{};
  co_await read_exactly(stream, header_bytes);
  const auto header = ntl::net::http2::inspect_header(
      ntl::net::scatter_view::from_contiguous(header_bytes),
      {maximum_message, false});
  if (!header)
    throw std::runtime_error("HTTP/2 adapter received an invalid frame header");
  wire_frame result;
  result.header = *header;
  result.wire.assign(header_bytes.begin(), header_bytes.end());
  result.wire.resize(header_bytes.size() + header->payload_size);
  if (header->payload_size != 0)
    co_await read_exactly(
        stream, std::span(result.wire).subspan(header_bytes.size()));
  co_return result;
}

ntl::net::http2::frame_view parse_frame(const wire_frame &wire) {
  auto parsed = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(wire.wire),
      {maximum_message, false});
  if (!parsed)
    throw std::runtime_error("HTTP/2 adapter frame parse failed");
  return *parsed;
}

sample::coroutine_task<std::size_t> write_frames(
    ntl::net::tls_stream &stream,
    std::span<const ntl::net::http2::outbound_frame> frames) {
  std::size_t total = 0;
  for (const auto &frame : frames) {
    if (co_await stream.write_all(frame.wire) != frame.wire.size())
      throw std::runtime_error("HTTP/2 adapter frame write was short");
    total += frame.wire.size();
  }
  co_return total;
}

ntl::net::http2::outbound_frame settings_frame(
    std::optional<std::uint32_t> initial_window = std::nullopt,
    bool enable_connect = false, bool acknowledgement = false) {
  std::vector<std::byte> payload;
  const auto append_setting = [&payload](std::uint16_t identifier,
                                         std::uint32_t value) {
    payload.push_back(static_cast<std::byte>((identifier >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(identifier & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 24) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 16) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(value & 0xffu));
  };
  if (initial_window)
    append_setting(0x4u, *initial_window);
  if (enable_connect)
    append_setting(0x8u, 1);
  auto frame = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::settings,
      acknowledgement ? std::uint8_t{0x01} : std::uint8_t{0}, 0, payload);
  if (!frame)
    throw std::runtime_error("HTTP/2 SETTINGS encoding failed");
  return std::move(*frame);
}

ntl::net::http2::outbound_frame data_frame(
    std::uint32_t stream_id, std::span<const std::byte> payload,
    bool end_stream) {
  auto frame = ntl::net::http2::transform_detail::make_frame(
      ntl::net::http2::frame_type::data,
      end_stream ? std::uint8_t{0x01} : std::uint8_t{0}, stream_id,
      payload);
  if (!frame)
    throw std::runtime_error("HTTP/2 DATA encoding failed");
  return std::move(*frame);
}

ntl::net::http::request_message request_message(
    std::string path, std::string method = "GET") {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = std::move(method);
  request.scheme = "https";
  request.authority = std::string(server_name_ascii);
  request.path = std::move(path);
  return request;
}

ntl::net::http::transform_pipeline make_pipeline() {
  ntl::net::http::transform_pipeline pipeline(
      {.maximum_header_count = 128,
       .maximum_header_bytes = 64 * 1024,
       .maximum_encoded_body_bytes = maximum_message,
       .maximum_decoded_body_bytes = maximum_message,
       .maximum_expansion_ratio = 32,
       .maximum_coding_layers = 4,
       .on_failure = ntl::net::http::transform_failure_policy::block});
  pipeline.requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-ntl-adapter", "1");
        return ntl::net::http::rewrite_result::headers_changed();
      });
  pipeline.requests().decide(
      [](const ntl::net::http::request_message &request) {
        return request.path == "/blocked"
                   ? ntl::net::inspection::verdict::block
                   : ntl::net::inspection::verdict::permit;
      });
  pipeline.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        constexpr std::string_view marker = "<!-- ntl-http2-adapter -->";
        response.body.insert(
            response.body.end(),
            reinterpret_cast<const std::byte *>(marker.data()),
            reinterpret_cast<const std::byte *>(marker.data() + marker.size()));
        return ntl::net::http::rewrite_result::replace_body(
            std::move(response.body),
            ntl::net::http::transformed_body_coding::identity);
      });
  return pipeline;
}

sample::coroutine_task<browser::browser_proxy_result> run_proxy(
    SOCKET inbound_socket, SOCKET outbound_socket,
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders,
    std::shared_ptr<const ntl::net::http::transform_pipeline> pipeline,
    std::shared_ptr<browser::browser_html_logger> logger) {
  co_await inbound->handshake_server(
      {.application_protocols = {"h2"},
       .require_application_protocol = true});
  require(inbound->negotiated_application_protocol() == "h2",
          "HTTP/2 proxy inbound ALPN was not h2");
  proxy_progress.store(1, std::memory_order_release);
  co_await outbound->handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"h2"},
       .require_application_protocol = true});
  require(outbound->negotiated_application_protocol() == "h2",
          "HTTP/2 proxy outbound ALPN was not h2");
  proxy_progress.store(2, std::memory_order_release);
  ntl::net::http::inspection_session_metadata metadata;
  metadata.connection.flow_direction =
      ntl::net::inspection::direction::outbound;
  metadata.tls.server_name = std::string(server_name_ascii);
  metadata.tls.alpn = "h2";
  auto policy = std::make_shared<ntl::net::http::inspection_policy>(
      pipeline->limits_ref());
  policy->use_content_codecs(std::move(decoders), std::move(encoders));
  policy->transforms_ref() = *pipeline;
  auto dispatcher = browser::make_browser_http_dispatcher(
      std::move(policy), std::move(logger));
  auto operation = dispatcher->run(
      ntl::net::user::inspected_http_protocol::http2,
      std::move(inbound), std::move(outbound), metadata);
  dispatcher.reset();
  const auto summary = co_await std::move(operation);
  proxy_progress.store(3, std::memory_order_release);
  co_return browser::browser_proxy_result{
      std::wstring(server_name), summary.last_status, std::nullopt};
}

struct multiplex_origin_result {
  bool stream_window_released = false;
  bool transformed = true;
  bool blocked_stream_reached_origin = false;
  std::unordered_map<std::uint32_t, std::string> bodies;
};

sample::coroutine_task<multiplex_origin_result> run_multiplex_origin(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_server(
      {.application_protocols = {"h2"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "h2",
          "HTTP/2 origin ALPN was not h2");
  origin_progress.store(1, std::memory_order_release);
  std::array<std::byte, client_preface.size()> preface{};
  co_await read_exactly(stream, preface);
  require(text_of(preface) == client_preface,
          "HTTP/2 origin received a bad client preface");
  const std::array initial{settings_frame(8)};
  co_await write_frames(stream, initial);
  origin_progress.store(2, std::memory_order_release);

  ntl::net::http::transform_pipeline passthrough;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests,
      exchanges, passthrough, decoders, encoders);
  multiplex_origin_result result;
  bool updated_stream = false;
  for (unsigned count = 0; count != 128 && result.bodies.size() != 2;
       ++count) {
    auto wire = co_await read_frame(stream);
    origin_progress.fetch_add(1, std::memory_order_acq_rel);
    const auto frame = parse_frame(wire);
    if (wire.header.type == ntl::net::http2::frame_type::settings) {
      if (!wire.header.acknowledgement()) {
        const std::array acknowledgement{
            settings_frame({}, false, true)};
        co_await write_frames(stream, acknowledgement);
      }
      continue;
    }
    if (wire.header.stream_id == 7)
      result.blocked_stream_reached_origin = true;
    if (!updated_stream && wire.header.stream_id == 1 &&
        wire.header.type == ntl::net::http2::frame_type::headers) {
      auto credit = ntl::net::http2::encode_window_update(1, 64);
      if (!credit)
        throw std::runtime_error("HTTP/2 stream WINDOW_UPDATE encoding failed");
      const std::array credits{std::move(*credit)};
      co_await write_frames(stream, credits);
      updated_stream = true;
      result.stream_window_released = true;
    }
    auto transformed = requests.consume(frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 origin request decode failed");
    if (transformed->message_complete && transformed->request) {
      result.transformed =
          result.transformed &&
          transformed->request->headers.first("x-ntl-adapter") == "1";
      result.bodies[transformed->stream_id] =
          text_of(transformed->request->body);
    }
  }
  require(result.bodies.size() == 2,
          "HTTP/2 origin did not receive both multiplexed requests");
  origin_progress.store(40, std::memory_order_release);

  const auto send_response = [&stream](
                                 std::uint32_t stream_id,
                                 std::string_view label)
      -> sample::coroutine_task<std::size_t> {
    const std::string body =
        "<html><body>" + std::string(label) + "</body></html>";
    ntl::net::http::response_message response;
    response.wire_protocol = ntl::net::http::protocol::http2;
    response.status = 200;
    response.headers.append("content-type", "text/html; charset=utf-8");
    response.headers.append("content-length", std::to_string(body.size()));
    auto encoded = ntl::net::http2::encode_response_frames(
        stream_id, response, bytes_of(body));
    if (!encoded)
      throw std::runtime_error("HTTP/2 response encoding failed");
    co_return co_await write_frames(stream, *encoded);
  };
  co_await send_response(3, "stream-three");
  co_await send_response(1, "stream-one");
  origin_progress.store(50, std::memory_order_release);
  co_await stream.shutdown();
  origin_progress.store(60, std::memory_order_release);
  co_return result;
}

struct multiplex_client_result {
  std::unordered_map<std::uint32_t, unsigned> statuses;
  std::unordered_map<std::uint32_t, std::string> bodies;
  std::vector<std::uint32_t> completion_order;
  bool source_window_replenished = false;
};

sample::coroutine_task<multiplex_client_result> run_multiplex_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"h2"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "h2",
          "HTTP/2 client ALPN was not h2");
  client_progress.store(1, std::memory_order_release);
  const auto preface = bytes_of(client_preface);
  if (co_await stream.write_all(preface) != preface.size())
    throw std::runtime_error("HTTP/2 client preface write was short");
  const std::array settings{settings_frame()};
  co_await write_frames(stream, settings);

  auto one = request_message("/one", "POST");
  const std::string request_body(32, 'x');
  one.headers.append("content-length", std::to_string(request_body.size()));
  auto three = request_message("/three");
  auto blocked = request_message("/blocked");
  auto one_frames = ntl::net::http2::encode_request_frames(
      1, one, bytes_of(request_body));
  auto three_frames = ntl::net::http2::encode_request_frames(3, three, {});
  auto blocked_frames = ntl::net::http2::encode_request_frames(7, blocked, {});
  if (!one_frames || !three_frames || !blocked_frames ||
      one_frames->size() != 2)
    throw std::runtime_error("HTTP/2 multiplex request encoding failed");
  co_await write_frames(stream, std::span(*one_frames).first(1));
  co_await write_frames(stream, *three_frames);
  co_await write_frames(stream, *blocked_frames);
  co_await write_frames(stream, std::span(*one_frames).subspan(1));
  client_progress.store(2, std::memory_order_release);

  ntl::net::http::transform_pipeline passthrough;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  require(exchanges->remember(1, one).is_ok() &&
              exchanges->remember(3, three).is_ok() &&
              exchanges->remember(7, blocked).is_ok(),
          "HTTP/2 client exchange setup failed");
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses,
      exchanges, passthrough, decoders, encoders);
  multiplex_client_result result;
  for (unsigned count = 0; count != 192 && result.statuses.size() != 3;
       ++count) {
    auto wire = co_await read_frame(stream);
    client_progress.fetch_add(1, std::memory_order_acq_rel);
    const auto frame = parse_frame(wire);
    if (wire.header.type == ntl::net::http2::frame_type::settings) {
      if (!wire.header.acknowledgement()) {
        const std::array acknowledgement{
            settings_frame({}, false, true)};
        co_await write_frames(stream, acknowledgement);
      }
      continue;
    }
    if (wire.header.type == ntl::net::http2::frame_type::window_update &&
        (wire.header.stream_id == 0 || wire.header.stream_id == 1))
      result.source_window_replenished = true;
    auto transformed = responses.consume(frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 client response decode failed");
    if (transformed->message_complete && transformed->response) {
      result.statuses[transformed->stream_id] =
          transformed->response->status;
      result.bodies[transformed->stream_id] =
          text_of(transformed->response->body);
      result.completion_order.push_back(transformed->stream_id);
    }
  }
  client_progress.store(50, std::memory_order_release);
  co_await stream.shutdown();
  client_progress.store(60, std::memory_order_release);
  co_return result;
}

ntl::net::websocket::permessage_deflate_parameters websocket_compression() {
  return {.enabled = true,
          .client_no_context_takeover = true,
          .server_no_context_takeover = true,
          .client_max_window_bits = 15,
          .server_max_window_bits = 15};
}

std::vector<std::byte> websocket_frame(
    ntl::net::websocket::opcode operation, std::string_view payload,
    ntl::net::websocket::sender_role sender, bool compressed = false) {
  std::vector<std::byte> encoded_payload = bytes_of(payload);
  if (compressed) {
    auto encoder = ntl::net::websocket::make_permessage_deflate_encoder(
        websocket_compression(), sender, maximum_message, maximum_message);
    auto encoded = encoder.encode(encoded_payload);
    if (!encoded)
      throw std::runtime_error("HTTP/2 WebSocket compression failed");
    encoded_payload = std::move(*encoded);
  }
  const auto mask_provider = [] {
    return std::array<std::byte, 4>{std::byte{0x11}, std::byte{0x22},
                                    std::byte{0x33}, std::byte{0x44}};
  };
  auto wire = ntl::net::websocket::transform_detail::encode_frame(
      operation, true, compressed ? std::uint8_t{0x04} : std::uint8_t{0},
      encoded_payload, sender,
      sender == ntl::net::websocket::sender_role::client
          ? std::function<std::array<std::byte, 4>()>(mask_provider)
          : std::function<std::array<std::byte, 4>()>{});
  if (!wire)
    throw std::runtime_error("HTTP/2 WebSocket frame encoding failed");
  return std::move(*wire);
}

struct websocket_observation {
  std::string text;
  bool close = false;
};

class websocket_consumer {
public:
  explicit websocket_consumer(ntl::net::websocket::sender_role sender)
      : sender_(sender) {
    pipeline_.inspect([this](const ntl::net::websocket::message &message) {
      if (message.operation == ntl::net::websocket::opcode::text)
        observed_.text = text_of(message.payload);
      if (message.operation == ntl::net::websocket::opcode::close)
        observed_.close = true;
      return ntl::net::inspection::verdict::permit;
    });
    const auto mask_provider = [] {
      return std::array<std::byte, 4>{std::byte{0x55}, std::byte{0x66},
                                      std::byte{0x77}, std::byte{0x88}};
    };
    transformer_ = std::make_unique<ntl::net::websocket::stream_transformer>(
        sender_, pipeline_, websocket_compression(),
        sender_ == ntl::net::websocket::sender_role::client
            ? std::function<std::array<std::byte, 4>()>(mask_provider)
            : std::function<std::array<std::byte, 4>()>{},
        ntl::net::websocket::stream_transform_options{
            maximum_message + 14, maximum_message});
  }

  void consume(ntl::net::scatter_view payload, bool end_stream) {
    auto transformed = transformer_->consume(payload, end_stream);
    if (!transformed)
      throw std::runtime_error("HTTP/2 WebSocket stream decode failed");
  }

  const websocket_observation &observed() const noexcept {
    return observed_;
  }

private:
  ntl::net::websocket::sender_role sender_;
  ntl::net::websocket::message_transform_pipeline pipeline_;
  websocket_observation observed_;
  std::unique_ptr<ntl::net::websocket::stream_transformer> transformer_;
};

sample::coroutine_task<websocket_observation> run_extended_origin(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_server(
      {.application_protocols = {"h2"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "h2",
          "HTTP/2 Extended CONNECT origin ALPN was not h2");
  std::array<std::byte, client_preface.size()> preface{};
  co_await read_exactly(stream, preface);
  require(text_of(preface) == client_preface,
          "HTTP/2 Extended CONNECT origin received a bad preface");
  const std::array settings{settings_frame({}, true)};
  co_await write_frames(stream, settings);

  ntl::net::http::transform_pipeline passthrough;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  exchanges->peer_extended_connect_enabled(true);
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests,
      exchanges, passthrough, decoders, encoders);
  websocket_consumer websocket(ntl::net::websocket::sender_role::client);
  bool established = false;
  for (unsigned count = 0; count != 128 && !websocket.observed().close;
       ++count) {
    auto wire = co_await read_frame(stream);
    const auto frame = parse_frame(wire);
    if (wire.header.type == ntl::net::http2::frame_type::settings) {
      if (!wire.header.acknowledgement()) {
        const std::array acknowledgement{
            settings_frame({}, false, true)};
        co_await write_frames(stream, acknowledgement);
      }
      continue;
    }
    auto transformed = requests.consume(frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 Extended CONNECT request failed");
    if (transformed->message_complete && transformed->request) {
      require(transformed->request->extended_protocol == "websocket" &&
                  transformed->request->headers.first("x-ntl-adapter") == "1",
              "HTTP/2 Extended CONNECT metadata was not transformed");
      require(exchanges->admit_connect(
                  wire.header.stream_id,
                  ntl::net::http2::connect_disposition::inspect)
                  .is_ok(),
              "HTTP/2 Extended CONNECT was not explicitly admitted");
      ntl::net::http::response_message response;
      response.wire_protocol = ntl::net::http::protocol::http2;
      response.status = 200;
      response.headers.append(
          "sec-websocket-extensions",
          "permessage-deflate; client_no_context_takeover; "
          "server_no_context_takeover");
      auto encoded = ntl::net::http2::encode_response_frames(
          wire.header.stream_id, response, {},
          ntl::net::http2::default_maximum_frame_size, 256 * 1024,
          false, false);
      if (!encoded)
        throw std::runtime_error("HTTP/2 Extended CONNECT response failed");
      co_await write_frames(stream, *encoded);
      established = true;
      continue;
    }
    if (established && wire.header.type == ntl::net::http2::frame_type::data) {
      auto payload = frame.data_payload();
      if (!payload)
        throw std::runtime_error("HTTP/2 Extended CONNECT DATA was invalid");
      websocket.consume(*payload, wire.header.end_stream());
    }
  }
  require(websocket.observed().text == "client-h2-websocket" &&
              websocket.observed().close,
          "HTTP/2 adapter lost fragmented client WebSocket data");

  auto text = websocket_frame(ntl::net::websocket::opcode::text,
                              "origin-h2-websocket",
                              ntl::net::websocket::sender_role::server, true);
  auto close = websocket_frame(ntl::net::websocket::opcode::close, {},
                               ntl::net::websocket::sender_role::server);
  const std::size_t split = text.size() / 2;
  const std::array frames{
      data_frame(1, std::span(text).first(split), false),
      data_frame(1, std::span(text).subspan(split), false),
      data_frame(1, close, true)};
  co_await write_frames(stream, frames);
  co_await stream.shutdown();
  co_return websocket.observed();
}

sample::coroutine_task<websocket_observation> run_extended_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"h2"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "h2",
          "HTTP/2 Extended CONNECT client ALPN was not h2");
  const auto preface = bytes_of(client_preface);
  if (co_await stream.write_all(preface) != preface.size())
    throw std::runtime_error("HTTP/2 Extended CONNECT preface was short");
  const std::array settings{settings_frame({}, true)};
  co_await write_frames(stream, settings);

  ntl::net::http::request_message request = request_message("/socket", "CONNECT");
  request.extended_protocol = "websocket";
  request.headers.append(
      "sec-websocket-extensions",
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover");
  auto encoded = ntl::net::http2::encode_request_frames(
      1, request, {}, ntl::net::http2::default_maximum_frame_size,
      256 * 1024, false);
  if (!encoded)
    throw std::runtime_error("HTTP/2 Extended CONNECT request encoding failed");
  co_await write_frames(stream, *encoded);

  ntl::net::http::transform_pipeline passthrough;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  require(exchanges->remember(1, request).is_ok(),
          "HTTP/2 Extended CONNECT client state failed");
  require(exchanges->admit_connect(
              1, ntl::net::http2::connect_disposition::inspect)
              .is_ok(),
          "HTTP/2 Extended CONNECT response state was not admitted");
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses,
      exchanges, passthrough, decoders, encoders);
  bool established = false;
  websocket_consumer websocket(ntl::net::websocket::sender_role::server);
  for (unsigned count = 0; count != 128 && !established; ++count) {
    auto wire = co_await read_frame(stream);
    const auto frame = parse_frame(wire);
    if (wire.header.type == ntl::net::http2::frame_type::settings) {
      if (!wire.header.acknowledgement()) {
        const std::array acknowledgement{
            settings_frame({}, false, true)};
        co_await write_frames(stream, acknowledgement);
      }
      continue;
    }
    auto transformed = responses.consume(frame);
    if (!transformed)
      throw std::runtime_error("HTTP/2 Extended CONNECT response decode failed");
    if (transformed->message_complete && transformed->response) {
      require(transformed->response->status == 200,
              "HTTP/2 Extended CONNECT was not established");
      established = true;
    }
  }
  require(established, "HTTP/2 Extended CONNECT response was not received");

  auto text = websocket_frame(ntl::net::websocket::opcode::text,
                              "client-h2-websocket",
                              ntl::net::websocket::sender_role::client, true);
  auto close = websocket_frame(ntl::net::websocket::opcode::close, {},
                               ntl::net::websocket::sender_role::client);
  const std::size_t split = text.size() / 2;
  const std::array frames{
      data_frame(1, std::span(text).first(split), false),
      data_frame(1, std::span(text).subspan(split), false),
      data_frame(1, close, true)};
  co_await write_frames(stream, frames);

  for (unsigned count = 0; count != 128 && !websocket.observed().close;
       ++count) {
    auto wire = co_await read_frame(stream);
    const auto frame = parse_frame(wire);
    if (wire.header.type == ntl::net::http2::frame_type::settings)
      continue;
    if (wire.header.type != ntl::net::http2::frame_type::data)
      continue;
    auto payload = frame.data_payload();
    if (!payload)
      throw std::runtime_error("HTTP/2 WebSocket response DATA was invalid");
    websocket.consume(*payload, wire.header.end_stream());
  }
  co_await stream.shutdown();
  co_return websocket.observed();
}

struct tls_fixture {
  sample::ephemeral_certificate authority{false};
  ntl::net::windows_tls_certificate_issuer origin_issuer{
      authority.get(), {.key_name_prefix = L"crtsys-http2-origin",
                        .rsa_bits = 2048, .validity_days = 1,
                        .machine_keys = false}};
  ntl::net::windows_tls_certificate_issuer proxy_issuer{
      authority.get(), {.key_name_prefix = L"crtsys-http2-proxy",
                        .rsa_bits = 2048, .validity_days = 1,
                        .machine_keys = false}};
  std::shared_ptr<ntl::net::tls_server_identity> origin_identity =
      std::make_shared<ntl::net::tls_server_identity>(
          origin_issuer.issue(server_name));
  std::shared_ptr<ntl::net::tls_server_identity> proxy_identity =
      std::make_shared<ntl::net::tls_server_identity>(
          proxy_issuer.issue(server_name));
  ntl::net::tls_credentials browser_client_credentials =
      ntl::net::tls_credentials::client({.manual_peer_validation = true});
  ntl::net::tls_credentials proxy_client_credentials =
      ntl::net::tls_credentials::client({.manual_peer_validation = true});
  std::shared_ptr<ntl::net::tls_peer_certificate_policy> peer_policy =
      std::make_shared<ntl::net::certificate_authority_policy>(authority.get());
};

template <class OriginTask, class ClientTask>
auto run_exchange(tls_fixture &certificates,
                  std::shared_ptr<browser::browser_html_logger> logger,
                  OriginTask start_origin, ClientTask start_client) {
  auto proxy_listener = sample::make_listener();
  auto origin_listener = sample::make_listener();
  auto browser_native = connect_loopback(proxy_listener.port);
  auto proxy_inbound_native = sample::accept_one(proxy_listener);
  auto proxy_outbound_native = connect_loopback(origin_listener.port);
  auto origin_native = sample::accept_one(origin_listener);

  ntl::net::io_completion_context context;
  ntl::net::async_socket browser_socket(context, browser_native.release());
  ntl::net::async_socket proxy_inbound(context, proxy_inbound_native.release());
  ntl::net::async_socket proxy_outbound(context, proxy_outbound_native.release());
  ntl::net::async_socket origin_socket(context, origin_native.release());
  ntl::net::tls_stream browser_tls(browser_socket,
                                   certificates.browser_client_credentials);
  auto proxy_inbound_tls = std::make_shared<ntl::net::tls_stream>(
      proxy_inbound, certificates.proxy_identity->credentials(),
      ntl::net::tls_stream_limits{.receive_chunk_size = 257});
  auto proxy_outbound_tls = std::make_shared<ntl::net::tls_stream>(
      proxy_outbound, certificates.proxy_client_credentials,
      ntl::net::tls_stream_limits{.receive_chunk_size = 257});
  ntl::net::tls_stream origin_tls(
      origin_socket, certificates.origin_identity->credentials());

  auto decoders =
      std::make_shared<ntl::net::inspection::content_decoder_registry>();
  auto encoders =
      std::make_shared<ntl::net::inspection::content_encoder_registry>();
  ntl::net::inspection::register_standard_content_decoders(*decoders);
  ntl::net::inspection::register_standard_content_encoders(*encoders);
  auto pipeline =
      std::make_shared<ntl::net::http::transform_pipeline>(make_pipeline());
  auto origin = start_origin(origin_tls);
  auto proxy = run_proxy(
      proxy_inbound.borrowed_native_handle(),
      proxy_outbound.borrowed_native_handle(),
      proxy_inbound_tls, proxy_outbound_tls, certificates.peer_policy,
      decoders, encoders, pipeline, logger);
  auto client = start_client(browser_tls, certificates.peer_policy);
  using client_result_type = decltype(client.get());
  using origin_result_type = decltype(origin.get());
  std::optional<browser::browser_proxy_result> proxy_result;
  std::optional<client_result_type> client_result;
  std::optional<origin_result_type> origin_result;
  std::exception_ptr proxy_failure;
  std::exception_ptr client_failure;
  std::exception_ptr origin_failure;
  const auto stop_all = [&] {
    (void)::shutdown(browser_socket.borrowed_native_handle(), SD_BOTH);
    (void)::shutdown(proxy_inbound.borrowed_native_handle(), SD_BOTH);
    (void)::shutdown(proxy_outbound.borrowed_native_handle(), SD_BOTH);
    (void)::shutdown(origin_socket.borrowed_native_handle(), SD_BOTH);
  };
  std::atomic<bool> timed_out = false;
  std::atomic<unsigned> timeout_proxy = 0;
  std::atomic<unsigned> timeout_origin = 0;
  std::atomic<unsigned> timeout_client = 0;
  std::promise<void> collectors_done;
  auto completion = collectors_done.get_future();
  std::thread watchdog([&] {
    if (completion.wait_for(std::chrono::seconds(15)) ==
        std::future_status::timeout) {
      timeout_proxy.store(
          proxy_progress.load(std::memory_order_acquire),
          std::memory_order_release);
      timeout_origin.store(
          origin_progress.load(std::memory_order_acquire),
          std::memory_order_release);
      timeout_client.store(
          client_progress.load(std::memory_order_acquire),
          std::memory_order_release);
      timed_out.store(true, std::memory_order_release);
      stop_all();
    }
  });
  std::thread proxy_collector([&] {
    try {
      proxy_result.emplace(proxy.get());
    } catch (...) {
      proxy_failure = std::current_exception();
      stop_all();
    }
  });
  std::thread client_collector([&] {
    try {
      client_result.emplace(client.get());
    } catch (...) {
      client_failure = std::current_exception();
      stop_all();
    }
  });
  std::thread origin_collector([&] {
    try {
      origin_result.emplace(origin.get());
    } catch (...) {
      origin_failure = std::current_exception();
      stop_all();
    }
  });
  proxy_collector.join();
  client_collector.join();
  origin_collector.join();
  collectors_done.set_value();
  watchdog.join();
  context.wait_for_idle();
  if (timed_out.load(std::memory_order_acquire))
    throw std::runtime_error(
        "HTTP/2 exchange exceeded the 15-second watchdog (proxy=" +
        std::to_string(timeout_proxy.load(std::memory_order_acquire)) +
        ", origin=" +
        std::to_string(timeout_origin.load(std::memory_order_acquire)) +
        ", client=" +
        std::to_string(timeout_client.load(std::memory_order_acquire)) +
        ")");
  if (proxy_failure || client_failure || origin_failure) {
    const auto describe = [](const std::exception_ptr &failure) {
      if (!failure)
        return std::string("ok");
      try {
        std::rethrow_exception(failure);
      } catch (const std::exception &error) {
        return std::string(error.what());
      } catch (...) {
        return std::string("non-standard exception");
      }
    };
    throw std::runtime_error(
        "HTTP/2 exchange failed: proxy=" + describe(proxy_failure) +
        "; client=" + describe(client_failure) +
        "; origin=" + describe(origin_failure) + " (progress proxy=" +
        std::to_string(proxy_progress.load(std::memory_order_acquire)) +
        ", origin=" +
        std::to_string(origin_progress.load(std::memory_order_acquire)) +
        ", client=" +
        std::to_string(client_progress.load(std::memory_order_acquire)) +
        ")");
  }
  return std::tuple(std::move(*client_result), std::move(*proxy_result),
                    std::move(*origin_result));
}

void test_multiplex_flow_and_policy() {
  proxy_progress.store(0, std::memory_order_release);
  origin_progress.store(0, std::memory_order_release);
  client_progress.store(0, std::memory_order_release);
  temporary_directory output;
  auto logger =
      std::make_shared<browser::browser_html_logger>(output.get());
  tls_fixture certificates;
  const auto [client, proxy, origin] = run_exchange(
      certificates, logger,
      [](ntl::net::tls_stream &stream) { return run_multiplex_origin(stream); },
      [](ntl::net::tls_stream &stream,
         std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy) {
        return run_multiplex_client(stream, policy);
      });
  require(origin.transformed && !origin.blocked_stream_reached_origin,
          "HTTP/2 request policy did not fail closed before the origin");
  require(origin.stream_window_released && client.source_window_replenished,
          "HTTP/2 flow-control credit path was not exercised");
  require(origin.bodies.at(1) == std::string(32, 'x') &&
              origin.bodies.at(3).empty(),
          "HTTP/2 multiplexed request bodies were corrupted");
  require(client.statuses.at(1) == 200 && client.statuses.at(3) == 200 &&
              client.statuses.at(7) == 403,
          "HTTP/2 policy returned wrong stream-local statuses");
  require(client.bodies.at(1).find("ntl-http2-adapter") != std::string::npos &&
              client.bodies.at(3).find("ntl-http2-adapter") != std::string::npos,
          "HTTP/2 response HTML transform was bypassed");
  require(client.completion_order.size() == 3,
          "HTTP/2 multiplexed responses did not complete independently");
  require(proxy.status == 200 || proxy.status == 403,
          "HTTP/2 adapter returned no completed response observation");
}

void test_extended_connect_websocket() {
  proxy_progress.store(0, std::memory_order_release);
  origin_progress.store(0, std::memory_order_release);
  client_progress.store(0, std::memory_order_release);
  temporary_directory output;
  auto logger =
      std::make_shared<browser::browser_html_logger>(output.get());
  tls_fixture certificates;
  const auto [client, proxy, origin] = run_exchange(
      certificates, logger,
      [](ntl::net::tls_stream &stream) { return run_extended_origin(stream); },
      [](ntl::net::tls_stream &stream,
         std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy) {
        return run_extended_client(stream, policy);
      });
  require(origin.text == "client-h2-websocket" && origin.close,
          "HTTP/2 adapter lost the client Extended CONNECT stream");
  require(client.text == "origin-h2-websocket" && client.close,
          "HTTP/2 adapter lost the origin Extended CONNECT stream");
  require(proxy.status == 200,
          "HTTP/2 adapter did not observe the Extended CONNECT response");
}

} // namespace

int main() {
  try {
    sample::winsock_session winsock;
    test_multiplex_flow_and_policy();
    test_extended_connect_websocket();
    std::cout << "HTTP/2 actual adapter contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
