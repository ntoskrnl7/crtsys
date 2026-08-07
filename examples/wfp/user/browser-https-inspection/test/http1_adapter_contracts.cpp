#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include <ntl/net/http/http1_framing>
#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/http1_proxy_connection>
#include <ntl/net/http/http1_transform>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/io/async_socket>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/net/websocket/framing>
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
constexpr std::size_t maximum_message = 2 * 1024 * 1024;

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
    sample::throw_socket("WSASocketW(HTTP/1 adapter)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    sample::throw_socket("connect(HTTP/1 adapter)");
  return socket;
}

class temporary_directory {
public:
  temporary_directory() {
    root_ = std::filesystem::temp_directory_path() /
            (L"crtsys-http1-adapter-" +
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

struct framed_message {
  std::vector<std::byte> wire;
  std::vector<std::byte> carry;
};

sample::coroutine_task<framed_message> read_http1(
    ntl::net::tls_stream &stream,
    ntl::net::http::http1_message_kind kind,
    std::vector<std::byte> carry = {}, bool response_to_head = false) {
  ntl::net::http::http1_framing_limits limits;
  limits.maximum_header_size = 64 * 1024;
  limits.maximum_body_size = maximum_message;
  limits.maximum_chunk_line_size = 8 * 1024;
  limits.maximum_trailer_size = 64 * 1024;
  limits.allow_close_delimited_response = true;
  limits.response_body_forbidden = response_to_head;
  ntl::net::tls_framed_stream framed(
      stream, ntl::net::http::http1_message_framer(kind, limits),
      {maximum_message}, 4096);
  framed.append_buffered(carry);
  auto message = co_await framed.read_frame_or_eof();
  if (!message)
    throw std::runtime_error("HTTP/1 adapter stream ended before a message");
  framed_message result;
  result.wire.assign(message->frame().begin(), message->frame().end());
  result.carry = framed.release_buffered();
  co_return result;
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
  pipeline.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        constexpr std::string_view marker = "<!-- ntl-http1-adapter -->";
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

struct persistent_origin_result {
  std::vector<std::string> paths;
  bool transformed_header = true;
};

sample::coroutine_task<persistent_origin_result> run_persistent_origin(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "http/1.1",
          "HTTP/1 origin ALPN was not http/1.1");
  ntl::net::inspection::content_decoder_registry decoders;
  persistent_origin_result observed;
  std::vector<std::byte> carry;
  for (unsigned index = 0; index != 6; ++index) {
    auto wire = co_await read_http1(
        stream, ntl::net::http::http1_message_kind::request,
        std::move(carry));
    carry = std::move(wire.carry);
    auto request = ntl::net::http::parse_http1_request(
        wire.wire, decoders, {.origin_scheme = "https"},
        {.maximum_header_count = 128,
         .maximum_header_bytes = 64 * 1024,
         .maximum_encoded_body_bytes = maximum_message,
         .maximum_decoded_body_bytes = maximum_message});
    if (!request)
      throw std::runtime_error("HTTP/1 origin could not parse request");
    observed.paths.emplace_back(
        request->message.path.data(), request->message.path.size());
    observed.transformed_header =
        observed.transformed_header &&
        request->message.headers.first("x-ntl-adapter") == "1";

    std::string response;
    if (request->message.path == "/head") {
      response = "HTTP/1.1 200 OK\r\nContent-Length: 91\r\n\r\n";
    } else if (request->message.path == "/hints") {
      response =
          "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n"
          "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
          "Content-Length: 31\r\n\r\n"
          "<html><body>hints</body></html>";
    } else if (request->message.path == "/chunked") {
      response =
          "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
          "Transfer-Encoding: chunked\r\n\r\n"
          "c\r\n<html><body>\r\n"
          "e\r\nchunked</body>\r\n"
          "7\r\n</html>\r\n0\r\nX-Trailer: one\r\n\r\n";
    } else if (request->message.path == "/no-content") {
      response = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    } else if (request->message.path == "/not-modified") {
      response = "HTTP/1.1 304 Not Modified\r\nETag: adapter\r\n\r\n";
    } else if (request->message.path == "/close") {
      response =
          "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
          "Connection: close\r\n\r\nclose-delimited-body";
    } else {
      throw std::runtime_error("HTTP/1 origin received an unknown path");
    }
    const auto bytes = bytes_of(response);
    if (co_await stream.write_all(bytes) != bytes.size())
      throw std::runtime_error("HTTP/1 origin response write was short");
    if (request->message.path == "/close")
      break;
  }
  co_await stream.shutdown();
  co_return observed;
}

struct persistent_client_result {
  std::vector<unsigned> statuses;
  std::vector<std::string> bodies;
  unsigned informational = 0;
};

sample::coroutine_task<persistent_client_result> run_persistent_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "http/1.1",
          "HTTP/1 client ALPN was not http/1.1");
  const std::string requests =
      "HEAD /head HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
      "GET /hints HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
      "GET /chunked HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
      "GET /no-content HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
      "GET /not-modified HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
      "GET /close HTTP/1.1\r\nHost: adapter.example.test\r\nConnection: close\r\n\r\n";
  const auto wire = bytes_of(requests);
  if (co_await stream.write_all(wire) != wire.size())
    throw std::runtime_error("HTTP/1 pipelined request write was short");

  const std::array<std::string_view, 6> methods{
      "HEAD", "GET", "GET", "GET", "GET", "GET"};
  ntl::net::inspection::content_decoder_registry decoders;
  persistent_client_result observed;
  std::vector<std::byte> carry;
  for (const auto method : methods) {
    for (;;) {
      auto response_wire = co_await read_http1(
          stream, ntl::net::http::http1_message_kind::response,
          std::move(carry), method == "HEAD");
      carry = std::move(response_wire.carry);
      auto response = ntl::net::http::parse_http1_response(
          response_wire.wire, decoders,
          {.maximum_header_count = 128,
           .maximum_header_bytes = 64 * 1024,
           .maximum_encoded_body_bytes = maximum_message,
           .maximum_decoded_body_bytes = maximum_message},
          method == "HEAD");
      if (!response)
        throw std::runtime_error("HTTP/1 client could not parse response");
      if (response->message.status >= 100 &&
          response->message.status < 200 &&
          response->message.status != 101) {
        ++observed.informational;
        continue;
      }
      observed.statuses.push_back(response->message.status);
      observed.bodies.push_back(text_of(response->message.body));
      break;
    }
  }
  co_return observed;
}

sample::coroutine_task<browser::browser_proxy_result> run_http1_proxy(
    SOCKET inbound_socket, SOCKET outbound_socket,
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry> decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry> encoders,
    std::shared_ptr<const ntl::net::http::transform_pipeline> pipeline,
    std::shared_ptr<browser::browser_html_logger> logger) {
  co_await inbound->handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(inbound->negotiated_application_protocol() == "http/1.1",
          "HTTP/1 proxy inbound ALPN was not http/1.1");
  co_await outbound->handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(outbound->negotiated_application_protocol() == "http/1.1",
          "HTTP/1 proxy outbound ALPN was not http/1.1");
  ntl::net::http::inspection_session_metadata metadata;
  metadata.connection.flow_direction =
      ntl::net::inspection::direction::outbound;
  metadata.tls.server_name = std::string(server_name_ascii);
  metadata.tls.alpn = "http/1.1";
  auto policy = std::make_shared<ntl::net::http::inspection_policy>(
      pipeline->limits_ref());
  policy->use_content_codecs(std::move(decoders), std::move(encoders));
  policy->transforms_ref() = *pipeline;
  auto dispatcher = browser::make_browser_http_dispatcher(
      std::move(policy), std::move(logger));
  auto operation = dispatcher->run(
      ntl::net::user::inspected_http_protocol::http1,
      std::move(inbound), std::move(outbound), metadata);
  dispatcher.reset();
  const auto summary = co_await std::move(operation);
  co_return browser::browser_proxy_result{
      std::wstring(server_name), summary.last_status, std::nullopt};
}

struct websocket_result {
  std::string text;
  bool close = false;
};

std::vector<std::byte> websocket_frame(
    ntl::net::websocket::opcode operation, std::string_view payload,
    ntl::net::websocket::sender_role sender, bool compressed = false) {
  const auto fixed_mask = [] {
    return std::array<std::byte, 4>{std::byte{0x11}, std::byte{0x22},
                                    std::byte{0x33}, std::byte{0x44}};
  };
  std::vector<std::byte> encoded_payload = bytes_of(payload);
  if (compressed) {
    ntl::net::websocket::permessage_deflate_encoder encoder(
        15, true, 1024 * 1024, 1024 * 1024);
    auto deflated = encoder.encode(encoded_payload);
    if (!deflated)
      throw std::runtime_error(
          "WebSocket contract compression failed");
    encoded_payload = std::move(*deflated);
  }
  auto encoded = ntl::net::websocket::transform_detail::encode_frame(
      operation, true, compressed ? std::uint8_t{0x04} : std::uint8_t{0},
      encoded_payload, sender,
      sender == ntl::net::websocket::sender_role::client
          ? std::function<std::array<std::byte, 4>()>(fixed_mask)
          : std::function<std::array<std::byte, 4>()>{});
  if (!encoded)
    throw std::runtime_error("WebSocket contract frame encoding failed");
  return std::move(*encoded);
}

sample::coroutine_task<framed_message> read_websocket(
    ntl::net::tls_stream &stream,
    ntl::net::websocket::sender_role sender,
    std::vector<std::byte> carry = {}) {
  ntl::net::tls_framed_stream framed(
      stream, ntl::net::websocket::frame_framer(
                  sender, {.maximum_payload_size = 1024 * 1024,
                           .allowed_reserved_bits = 0x04}),
      {1024 * 1024 + 14}, 4096);
  framed.append_buffered(carry);
  auto message = co_await framed.read_frame_or_eof();
  if (!message)
    throw std::runtime_error("WebSocket adapter stream ended early");
  auto header = ntl::net::websocket::inspect_header(
      ntl::net::scatter_view::from_contiguous(message->frame()), sender,
      {.maximum_payload_size = 1024 * 1024,
       .allowed_reserved_bits = 0x04});
  if (!header)
    throw std::runtime_error("WebSocket adapter frame was invalid");
  framed_message result;
  result.wire.assign(message->frame().begin(), message->frame().end());
  result.carry = framed.release_buffered();
  co_return result;
}

websocket_result decode_websocket(
    std::span<const std::byte> wire,
    ntl::net::websocket::sender_role sender) {
  const auto view = ntl::net::scatter_view::from_contiguous(wire);
  auto header = ntl::net::websocket::inspect_header(
      view, sender, {.maximum_payload_size = 1024 * 1024,
                     .allowed_reserved_bits = 0x04});
  if (!header)
    throw std::runtime_error("WebSocket frame header was rejected");
  auto payload = ntl::net::websocket::decode_payload(view, *header, 1024 * 1024);
  if (!payload)
    throw std::runtime_error("WebSocket frame payload was rejected");
  std::vector<std::byte> decoded_payload = std::move(*payload);
  if ((header->reserved_bits & 0x04u) != 0) {
    ntl::net::websocket::permessage_deflate_decoder decoder(
        15, true, 1024 * 1024);
    auto inflated = decoder.decode(decoded_payload, true);
    if (!inflated)
      throw std::runtime_error(
          "WebSocket contract decompression failed");
    decoded_payload = std::move(*inflated);
  }
  return {text_of(decoded_payload),
          header->operation == ntl::net::websocket::opcode::close};
}

sample::coroutine_task<websocket_result> run_websocket_origin(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "http/1.1",
          "HTTP/1 WebSocket origin ALPN was not http/1.1");
  auto request = co_await read_http1(
      stream, ntl::net::http::http1_message_kind::request);
  std::vector<std::byte> response = bytes_of(
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\nUpgrade: websocket\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; "
      "client_no_context_takeover; server_no_context_takeover\r\n\r\n");
  auto text = websocket_frame(ntl::net::websocket::opcode::text,
                              "origin-carry",
                              ntl::net::websocket::sender_role::server, true);
  auto close = websocket_frame(ntl::net::websocket::opcode::close, {},
                               ntl::net::websocket::sender_role::server);
  response.insert(response.end(), text.begin(), text.end());
  response.insert(response.end(), close.begin(), close.end());
  if (co_await stream.write_all(response) != response.size())
    throw std::runtime_error("WebSocket origin response write was short");

  auto client_text = co_await read_websocket(
      stream, ntl::net::websocket::sender_role::client,
      std::move(request.carry));
  auto decoded = decode_websocket(
      client_text.wire, ntl::net::websocket::sender_role::client);
  auto client_close = co_await read_websocket(
      stream, ntl::net::websocket::sender_role::client,
      std::move(client_text.carry));
  decoded.close = decode_websocket(
                      client_close.wire,
                      ntl::net::websocket::sender_role::client)
                      .close;
  co_return decoded;
}

sample::coroutine_task<websocket_result> run_websocket_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> authority) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(server_name),
       .certificate_policy = std::move(authority),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  require(stream.negotiated_application_protocol() == "http/1.1",
          "HTTP/1 WebSocket client ALPN was not http/1.1");
  std::vector<std::byte> request = bytes_of(
      "GET /socket HTTP/1.1\r\nHost: adapter.example.test\r\n"
      "Connection: Upgrade\r\nUpgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: YWRhcHRlci1jb250cmFjdA==\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; "
      "client_no_context_takeover; server_no_context_takeover\r\n\r\n");
  auto text = websocket_frame(ntl::net::websocket::opcode::text,
                              "client-carry",
                              ntl::net::websocket::sender_role::client, true);
  auto close = websocket_frame(ntl::net::websocket::opcode::close, {},
                               ntl::net::websocket::sender_role::client);
  request.insert(request.end(), text.begin(), text.end());
  request.insert(request.end(), close.begin(), close.end());
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error("WebSocket client request write was short");

  auto response = co_await read_http1(
      stream, ntl::net::http::http1_message_kind::response);
  auto server_text = co_await read_websocket(
      stream, ntl::net::websocket::sender_role::server,
      std::move(response.carry));
  auto decoded = decode_websocket(
      server_text.wire, ntl::net::websocket::sender_role::server);
  auto server_close = co_await read_websocket(
      stream, ntl::net::websocket::sender_role::server,
      std::move(server_text.carry));
  decoded.close = decode_websocket(
                      server_close.wire,
                      ntl::net::websocket::sender_role::server)
                      .close;
  co_return decoded;
}

class memory_stream {
public:
  explicit memory_stream(std::string_view input = {})
      : input_(bytes_of(input)) {}

  ntl::net::user::task<std::size_t>
  read_some_borrowed(std::span<std::byte> destination) {
    const std::size_t remaining = input_.size() - input_offset_;
    const std::size_t count =
        (std::min)({remaining, destination.size(), std::size_t{7}});
    if (count != 0)
      std::copy_n(input_.data() + input_offset_, count, destination.data());
    input_offset_ += count;
    co_return count;
  }

  ntl::net::user::task<std::size_t>
  write_all(std::span<const std::byte> source) {
    written_.insert(written_.end(), source.begin(), source.end());
    co_return source.size();
  }

  ntl::net::user::task<void> shutdown() {
    ++shutdown_count_;
    co_return;
  }

  std::string written_text() const { return text_of(written_); }
  unsigned shutdown_count() const noexcept { return shutdown_count_; }

private:
  std::vector<std::byte> input_;
  std::size_t input_offset_ = 0;
  std::vector<std::byte> written_;
  unsigned shutdown_count_ = 0;
};

template <class Connection>
struct close_http1_from_observer
    : ntl::net::http::null_http1_observer {
  void on_inspection(
      const ntl::net::http::inspection_context_view &) noexcept {
    if (auto retained = owner.lock())
      retained->close();
  }

  std::weak_ptr<Connection> owner;
};

sample::coroutine_task<ntl::net::http::http1_proxy_result>
run_http1_owner_first_close_contract(
    std::shared_ptr<memory_stream> downstream,
    std::shared_ptr<memory_stream> upstream) {
  using connection_type =
      ntl::net::http::http1_proxy_connection<memory_stream, memory_stream>;
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  policy->use_content_codecs(decoders, encoders);
  auto connection = std::make_shared<connection_type>(
      downstream, upstream, policy,
      ntl::net::http::http1_request_target_context{
          .origin_scheme = "https"});
  close_http1_from_observer<connection_type> observer;
  observer.owner = connection;
  auto unexpected_upgrade = [](auto &)
      -> ntl::net::user::task<void> {
    throw std::runtime_error("unexpected HTTP/1 upgrade during close test");
    co_return;
  };
  auto operation = connection->run(observer, unexpected_upgrade);
  connection.reset();
  co_return co_await std::move(operation);
}

void test_http1_close_lifetime_contract() {
  auto downstream = std::make_shared<memory_stream>(
      "GET /close HTTP/1.1\r\n"
      "Host: adapter.example.test\r\n"
      "Connection: close\r\n\r\n");
  auto upstream = std::make_shared<memory_stream>();
  const auto result = run_http1_owner_first_close_contract(
                          downstream, upstream)
                          .get();
  require(result.termination ==
              ntl::net::http::http1_proxy_termination::closed &&
              downstream->shutdown_count() == 1 &&
              upstream->shutdown_count() == 1,
          "HTTP/1 owner-first callback close did not drain safely");

  using connection_type =
      ntl::net::http::http1_proxy_connection<memory_stream, memory_stream>;
  auto closed = std::make_shared<connection_type>(
      std::make_shared<memory_stream>(),
      std::make_shared<memory_stream>(),
      std::make_shared<ntl::net::http::inspection_policy>(),
      ntl::net::http::http1_request_target_context{
          .origin_scheme = "https"});
  closed->close();
  closed->close();
  bool rejected = false;
  try {
    auto operation = closed->run(
        ntl::net::http::null_http1_observer{},
        [](auto &) -> ntl::net::user::task<void> { co_return; });
    (void)operation;
  } catch (const std::system_error &error) {
    rejected = error.code().value() == ERROR_OPERATION_ABORTED;
  }
  require(rejected, "closed HTTP/1 proxy accepted a new run");
}

struct staged_context_observer : ntl::net::http::null_http1_observer {
  staged_context_observer(unsigned *headers_value,
                          unsigned *body_chunks_value,
                          unsigned *message_complete_value) noexcept
      : headers(headers_value), body_chunks(body_chunks_value),
        message_complete(message_complete_value) {}

  unsigned *headers = nullptr;
  unsigned *body_chunks = nullptr;
  unsigned *message_complete = nullptr;

  void on_inspection(
      const ntl::net::http::inspection_context_view &context) const {
    require(context.wire_protocol() == ntl::net::http::protocol::http1 &&
                context.stream_id() == 0 && context.exchange_id() == 1 &&
                context.direction() ==
                    ntl::net::http::message_direction::request,
            "HTTP/1 staged context lost protocol or exchange identity");
    require(context.connection().flow_id == 42 &&
                context.connection().flow_direction ==
                    ntl::net::inspection::direction::outbound &&
                context.connection().source &&
                context.connection().source->address == "192.0.2.10" &&
                context.connection().source->port == 51000 &&
                context.connection().destination &&
                context.connection().destination->address == "198.51.100.20" &&
                context.connection().destination->port == 443 &&
                context.connection().process_id == 9001 &&
                context.connection().application_label == "browser.exe" &&
                context.tls().server_name == "adapter.example.test" &&
                context.tls().alpn == "http/1.1",
            "HTTP/1 staged context lost WFP or TLS metadata");
    if (context.stage() == ntl::net::http::inspection_stage::headers)
      ++*headers;
    else if (context.stage() ==
             ntl::net::http::inspection_stage::body_chunk)
      ++*body_chunks;
    else
      ++*message_complete;
  }
};

struct counting_context_observer : ntl::net::http::null_http1_observer {
  counting_context_observer(unsigned *headers_value,
                            unsigned *body_chunks_value,
                            unsigned *message_complete_value) noexcept
      : headers(headers_value), body_chunks(body_chunks_value),
        message_complete(message_complete_value) {}

  unsigned *headers = nullptr;
  unsigned *body_chunks = nullptr;
  unsigned *message_complete = nullptr;

  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept {
    switch (context.stage()) {
    case ntl::net::http::inspection_stage::headers:
      ++*headers;
      break;
    case ntl::net::http::inspection_stage::body_chunk:
      ++*body_chunks;
      break;
    case ntl::net::http::inspection_stage::message_complete:
      ++*message_complete;
      break;
    }
  }
};

struct throwing_telemetry_observer {
  unsigned *inspection_calls = nullptr;
  unsigned *request_calls = nullptr;
  unsigned *response_calls = nullptr;

  void on_inspection(
      const ntl::net::http::inspection_context_view &) const {
    ++*inspection_calls;
    throw std::runtime_error("throwing inspection telemetry");
  }

  void on_request(
      const ntl::net::http::http1_request_event_view &) const {
    ++*request_calls;
    throw std::runtime_error("throwing request telemetry");
  }

  void on_response(
      const ntl::net::http::http1_response_event_view &) const {
    ++*response_calls;
    throw std::runtime_error("throwing response telemetry");
  }
};

template <class Downstream, class Upstream, class Observer>
sample::coroutine_task<ntl::net::http::http1_proxy_result>
run_memory_proxy(
    std::shared_ptr<Downstream> downstream,
    std::shared_ptr<Upstream> upstream,
    std::shared_ptr<ntl::net::http::inspection_policy> policy,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry> decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry> encoders,
    ntl::net::http::inspection_session_metadata metadata,
    Observer observer) {
  policy->use_content_codecs(decoders, encoders);
  ntl::net::http::http1_proxy_limits limits;
  limits.framing.maximum_header_size = 8 * 1024;
  limits.framing.maximum_body_size = 8 * 1024;
  limits.framing.maximum_chunk_line_size = 1024;
  limits.framing.maximum_trailer_size = 1024;
  limits.maximum_wire_message_size = 32 * 1024;
  auto connection = std::make_shared<
      ntl::net::http::http1_proxy_connection<Downstream, Upstream>>(
      std::move(downstream), std::move(upstream), std::move(policy),
      ntl::net::http::http1_request_target_context{.origin_scheme = "https"},
      limits, std::move(metadata));
  auto unexpected_upgrade = [](auto &)
      -> ntl::net::user::task<void> {
    throw std::runtime_error("unexpected HTTP/1 protocol upgrade");
    co_return;
  };
  co_return co_await connection->run(observer, unexpected_upgrade);
}

class continue_gated_stream {
public:
  continue_gated_stream(std::string_view head, std::string_view body)
      : input_(bytes_of(std::string(head) + std::string(body))),
        head_size_(head.size()), available_(head.size()) {}

  ntl::net::user::task<std::size_t>
  read_some_borrowed(std::span<std::byte> destination) {
    if (offset_ == available_ && available_ != input_.size())
      throw std::runtime_error(
          "HTTP/1 adapter attempted to read the gated body before 100 Continue");
    const std::size_t count = (std::min)(
        {available_ - offset_, destination.size(), std::size_t{7}});
    if (count)
      std::copy_n(input_.data() + offset_, count, destination.data());
    offset_ += count;
    co_return count;
  }

  ntl::net::user::task<std::size_t>
  write_all(std::span<const std::byte> source) {
    constexpr std::string_view provisional =
        "HTTP/1.1 100 Continue\r\n\r\n";
    const std::string text = text_of(source);
    if (available_ != input_.size()) {
      require(text == provisional,
              "HTTP/1 adapter emitted the wrong provisional response");
      available_ = input_.size();
      ++continue_count_;
    }
    written_.insert(written_.end(), source.begin(), source.end());
    co_return source.size();
  }

  ntl::net::user::task<void> shutdown() { co_return; }

  std::string written_text() const { return text_of(written_); }
  unsigned continue_count() const noexcept { return continue_count_; }

private:
  std::vector<std::byte> input_;
  std::size_t head_size_ = 0;
  std::size_t available_ = 0;
  std::size_t offset_ = 0;
  std::vector<std::byte> written_;
  unsigned continue_count_ = 0;
};

class persistent_continue_gated_stream {
public:
  persistent_continue_gated_stream(
      std::string_view first_head, std::string_view first_body,
      std::string_view second_head, std::string_view second_body) {
    const std::string input = std::string(first_head) +
                              std::string(first_body) +
                              std::string(second_head) +
                              std::string(second_body);
    input_ = bytes_of(input);
    gate_positions_ = {
        first_head.size(),
        first_head.size() + first_body.size() + second_head.size()};
    release_positions_ = {gate_positions_[1], input_.size()};
    available_ = gate_positions_[0];
  }

  ntl::net::user::task<std::size_t>
  read_some_borrowed(std::span<std::byte> destination) {
    if (offset_ == available_ && next_gate_ < gate_positions_.size()) {
      awaiting_continue_ = true;
      throw std::runtime_error(
          "HTTP/1 persistent adapter read a gated body before 100 Continue");
    }
    const std::size_t count = (std::min)(
        {available_ - offset_, destination.size(), std::size_t{7}});
    if (count)
      std::copy_n(input_.data() + offset_, count, destination.data());
    offset_ += count;
    if (offset_ == available_ && next_gate_ < gate_positions_.size())
      awaiting_continue_ = true;
    co_return count;
  }

  ntl::net::user::task<std::size_t>
  write_all(std::span<const std::byte> source) {
    constexpr std::string_view provisional =
        "HTTP/1.1 100 Continue\r\n\r\n";
    const std::string text = text_of(source);
    if (awaiting_continue_) {
      require(text == provisional,
              "HTTP/1 persistent adapter emitted the wrong provisional response");
      available_ = release_positions_[next_gate_++];
      awaiting_continue_ = false;
      ++continue_count_;
    }
    written_.insert(written_.end(), source.begin(), source.end());
    co_return source.size();
  }

  ntl::net::user::task<void> shutdown() { co_return; }

  std::string written_text() const { return text_of(written_); }
  unsigned continue_count() const noexcept { return continue_count_; }

private:
  std::vector<std::byte> input_;
  std::array<std::size_t, 2> gate_positions_{};
  std::array<std::size_t, 2> release_positions_{};
  std::size_t available_ = 0;
  std::size_t offset_ = 0;
  std::size_t next_gate_ = 0;
  bool awaiting_continue_ = false;
  std::vector<std::byte> written_;
  unsigned continue_count_ = 0;
};

void test_terminal_transform_and_empty_body_stages() {
  {
    auto downstream = std::make_shared<memory_stream>(
        "GET /terminal HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n"
        "GET /next HTTP/1.1\r\nHost: adapter.example.test\r\n"
        "Connection: close\r\n\r\n");
    auto upstream = std::make_shared<memory_stream>(
        "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    auto policy = std::make_shared<ntl::net::http::inspection_policy>();
    unsigned staged_decisions = 0;
    policy->transforms_ref().requests().transform(
        [](ntl::net::http::request_message &request) {
          if (request.path != "/terminal")
            return ntl::net::http::rewrite_result::unchanged();
          ntl::net::http::response_message response;
          response.wire_protocol = ntl::net::http::protocol::http1;
          response.status = 418;
          response.headers.append("content-type", "text/plain");
          response.body = bytes_of("teapot");
          return ntl::net::http::rewrite_result::respond(
              std::move(response));
        });
    policy->requests().at_headers().decide(
        [&staged_decisions](const ntl::net::http::inspection_context_view &) {
          ++staged_decisions;
          return ntl::net::inspection::verdict::permit;
        });
    unsigned headers = 0;
    unsigned body_chunks = 0;
    unsigned complete = 0;
    const auto result = run_memory_proxy(
                            downstream, upstream, policy, decoders, encoders,
                            {}, counting_context_observer{
                                    &headers, &body_chunks, &complete})
                            .get();
    require(result.termination ==
                    ntl::net::http::http1_proxy_termination::connection_close &&
                result.last_status == 204 && result.completed_exchanges == 2 &&
                result.blocked_exchanges == 1 && staged_decisions == 1 &&
                headers == 2 && body_chunks == 0 && complete == 2 &&
                upstream->written_text().find("GET /terminal") ==
                    std::string::npos &&
                upstream->written_text().find("GET /next") == 0 &&
                downstream->written_text().starts_with("HTTP/1.1 418") &&
                downstream->written_text().find("HTTP/1.1 204") !=
                    std::string::npos,
            "HTTP/1 synthetic block did not preserve persistent forwarding");
  }

  {
    auto downstream = std::make_shared<memory_stream>(
        "GET /empty HTTP/1.1\r\nHost: adapter.example.test\r\n\r\n");
    auto upstream = std::make_shared<memory_stream>(
        "HTTP/1.1 204 No Content\r\n\r\n");
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    auto policy = std::make_shared<ntl::net::http::inspection_policy>();
    unsigned headers = 0;
    unsigned body_chunks = 0;
    unsigned complete = 0;
    const auto result = run_memory_proxy(
                            downstream, upstream, policy, decoders, encoders,
                            {}, counting_context_observer{
                                    &headers, &body_chunks, &complete})
                            .get();
    require(result.completed_exchanges == 1 && result.last_status == 204 &&
                headers == 2 && body_chunks == 0 && complete == 2,
            "HTTP/1 empty semantic body emitted a body_chunk stage");
  }
}

void test_throwing_observer_cannot_change_forwarding() {
  auto downstream = std::make_shared<memory_stream>(
      "GET /observer HTTP/1.1\r\n"
      "Host: adapter.example.test\r\n"
      "Connection: close\r\n\r\n");
  auto upstream = std::make_shared<memory_stream>(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 2\r\n"
      "Connection: close\r\n\r\nok");
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  unsigned inspection_calls = 0;
  unsigned request_calls = 0;
  unsigned response_calls = 0;

  const auto result = run_memory_proxy(
                          downstream, upstream, policy, decoders, encoders,
                          {}, throwing_telemetry_observer{
                                  &inspection_calls, &request_calls,
                                  &response_calls})
                          .get();
  require(result.termination ==
                  ntl::net::http::http1_proxy_termination::connection_close &&
              result.completed_exchanges == 1 && result.last_status == 200 &&
              upstream->written_text().find("GET /observer HTTP/1.1") == 0 &&
              downstream->written_text().find("HTTP/1.1 200 OK") == 0 &&
              downstream->written_text().ends_with("ok"),
          "throwing HTTP/1 telemetry changed forwarding or policy outcome");
  require(inspection_calls == 5 && request_calls == 1 &&
              response_calls == 1,
          "HTTP/1 telemetry exception suppressed later observer phases");
}

void test_full_staged_context_policy() {
  auto downstream = std::make_shared<memory_stream>(
      "POST /inspect?mode=block HTTP/1.1\r\n"
      "Host: adapter.example.test\r\n"
      "X-Policy: benign-first\r\nX-Policy: inspect\r\n"
      "Custom-Feature: enabled\r\n"
      "Content-Length: 7\r\n\r\npayload");
  auto upstream = std::make_shared<memory_stream>();
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>(
      ntl::net::http::transform_limits{
       .maximum_header_count = 32,
       .maximum_header_bytes = 8 * 1024,
       .maximum_encoded_body_bytes = 8 * 1024,
       .maximum_decoded_body_bytes = 8 * 1024,
       .maximum_expansion_ratio = 8,
       .maximum_coding_layers = 2,
       .on_failure =
           ntl::net::http::transform_failure_policy::block});
  policy->transforms_ref().requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-ntl-transformed", "yes");
        return ntl::net::http::rewrite_result::headers_changed();
      });

  unsigned header_decisions = 0;
  unsigned body_decisions = 0;
  unsigned complete_decisions = 0;
  using namespace ntl::net::http::condition;
  policy->requests()
      .at_headers()
      .when(any_of(method_is("POST"), method_is("PUT")))
      .when(scheme_is("https"))
      .when(authority_is("adapter.example.test"))
      .when(path_is("/inspect"))
      .when(none_of(path_starts_with("/public")))
      .when(query_is("mode=block"))
      .when(header_is("x-policy", "inspect"))
      .when(header_is("x-ntl-transformed", "yes"))
      .when(header_name_starts_with("custom-"))
      .when(any_header([](const ntl::net::http::header_field &header) {
        return header.name.starts_with("custom-") &&
               header.value == "enabled";
      }))
      .when(process_is(9001))
      .when(tls_server_name_is("adapter.example.test"))
      .when(alpn_is("http/1.1"))
      .when(application_label_is("browser.exe"))
      .when(original_destination_port_is(443))
      .when([](const ntl::net::http::inspection_context_view &context) {
        return context.connection().flow_id == 42;
      })
      .decide([&](const ntl::net::http::inspection_context_view &) {
        ++header_decisions;
        return ntl::net::inspection::verdict::permit;
      });
  policy->requests()
      .at_body_chunk()
      .when(current_body_chunk_contains("payload"))
      .decide([&](const ntl::net::http::inspection_context_view &) {
        ++body_decisions;
        return ntl::net::inspection::verdict::permit;
      });
  policy->requests()
      .at_message_complete()
      .when(complete_body_contains("payload"))
      .when([](const ntl::net::http::inspection_context_view &context) {
        return context.trailers().empty();
      })
      .decide([&](const ntl::net::http::inspection_context_view &) {
        ++complete_decisions;
        return ntl::net::inspection::verdict::block;
      });

  ntl::net::http::inspection_session_metadata metadata;
  metadata.connection = {
      .flow_id = 42,
      .flow_direction = ntl::net::inspection::direction::outbound,
      .source = ntl::net::http::endpoint_metadata{"192.0.2.10", 51000},
      .destination =
          ntl::net::http::endpoint_metadata{"198.51.100.20", 443},
      .original_source =
          ntl::net::http::endpoint_metadata{"192.0.2.10", 51000},
      .original_destination =
          ntl::net::http::endpoint_metadata{"198.51.100.20", 443},
      .process_id = 9001,
      .application_label = "browser.exe"};
  metadata.tls = {.server_name = "adapter.example.test",
                  .alpn = "http/1.1"};
  unsigned observed_headers = 0;
  unsigned observed_body = 0;
  unsigned observed_complete = 0;
  const auto result = run_memory_proxy(
                          downstream, upstream, policy, decoders, encoders,
                          metadata,
                          staged_context_observer{
                              &observed_headers, &observed_body,
                              &observed_complete})
                          .get();
  require(result.termination ==
                  ntl::net::http::http1_proxy_termination::downstream_eof &&
              result.last_status == 403 && result.completed_exchanges == 1 &&
              result.blocked_exchanges == 1,
          "HTTP/1 complete-message decision did not fail closed");
  require(header_decisions == 1 && body_decisions == 1 &&
              complete_decisions == 1 && observed_headers == 1 &&
              observed_body == 1 && observed_complete == 1,
          "HTTP/1 staged policy or observer did not visit every phase");
  require(upstream->written_text().empty() &&
              downstream->written_text().find("HTTP/1.1 403") == 0,
          "HTTP/1 staged block leaked the request or omitted its response");
  require(downstream->shutdown_count() == 1 && upstream->shutdown_count() == 1,
          "HTTP/1 staged block did not shut down both streams");
}

void test_invalid_verdict_obeys_failure_policy() {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http1;
  request.method = "GET";
  request.scheme = "https";
  request.authority = "adapter.example.test";
  request.path = "/";
  ntl::net::http::inspection_session_metadata session;
  const auto context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::headers, session, request);

  const auto invalid = [](const ntl::net::http::inspection_context_view &) {
    return static_cast<ntl::net::inspection::verdict>(0x7f);
  };
  ntl::net::http::decision_policy closed;
  closed.requests().at_headers().decide(invalid);
  require(closed.evaluate(context) == ntl::net::inspection::verdict::block,
          "invalid HTTP inspection verdict did not fail closed");

  ntl::net::http::decision_policy open(
      ntl::net::inspection::failure_policy::fail_open);
  open.requests().at_headers().decide(invalid);
  require(open.evaluate(context) == ntl::net::inspection::verdict::permit,
          "invalid HTTP inspection verdict ignored explicit fail-open policy");
}

void test_first_match_permit_and_catch_all_block() {
  ntl::net::http::decision_policy policy;
  using namespace ntl::net::http::condition;
  unsigned catch_all_calls = 0;
  policy.requests()
      .at_headers()
      .when(path_is("/allowed"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::permit;
      });
  policy.requests().at_headers().decide(
      [&](const ntl::net::http::inspection_context_view &) {
        ++catch_all_calls;
        return ntl::net::inspection::verdict::block;
      });

  ntl::net::http::inspection_session_metadata session;
  ntl::net::http::request_message request;
  request.method = "GET";
  request.path = "/allowed";
  const auto evaluate = [&](std::string path) {
    request.path = std::move(path);
    const auto context = ntl::net::http::inspection_context_view::for_request(
        ntl::net::http::protocol::http1, 0, 1,
        ntl::net::http::inspection_stage::headers, session, request);
    return policy.evaluate(context);
  };
  require(evaluate("/allowed") == ntl::net::inspection::verdict::permit &&
              catch_all_calls == 0,
          "terminal permit did not stop ordered HTTP policy evaluation");
  require(evaluate("/other") == ntl::net::inspection::verdict::block &&
              catch_all_calls == 1,
          "catch-all HTTP block did not implement an allow-list default");
}

void test_rule_builder_lifetime_and_empty_decision() {
  using selector = ntl::net::http::inspection_rule_selector;
  static_assert(std::is_move_constructible_v<selector>);
  static_assert(!std::is_move_assignable_v<selector>);

  ntl::net::http::decision_policy policy;
  auto source = policy.requests().at_headers();
  source.when(ntl::net::http::condition::path_is("/moved"));
  auto destination = std::move(source);
  destination.decide([](const ntl::net::http::inspection_context_view &) {
    return ntl::net::inspection::verdict::block;
  });

  bool moved_from_rejected = false;
  try {
    source.decide([](const ntl::net::http::inspection_context_view &) {
      return ntl::net::inspection::verdict::permit;
    });
  } catch (const std::logic_error &) {
    moved_from_rejected = true;
  }
  require(moved_from_rejected,
          "moved-from HTTP inspection builder committed a second rule");

  ntl::net::http::inspection_session_metadata session;
  ntl::net::http::request_message request;
  request.method = "GET";
  request.path = "/moved";
  const auto context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::headers, session, request);
  require(policy.evaluate(context) == ntl::net::inspection::verdict::block,
          "moved HTTP inspection builder lost its configured condition");

  ntl::net::http::decision_policy empty_policy;
  std::function<ntl::net::inspection::verdict(
      const ntl::net::http::inspection_context_view &)> empty;
  bool empty_rejected = false;
  try {
    empty_policy.requests().at_headers().decide(empty);
  } catch (const std::invalid_argument &) {
    empty_rejected = true;
  }
  require(empty_rejected,
          "empty HTTP inspection decision was accepted at registration");
  require(empty_policy.evaluate(context) ==
              ntl::net::inspection::verdict::permit,
          "rejected empty HTTP inspection decision left a partial rule");

  // A selector is an owning child. Destroying the facade first must neither
  // invalidate the builder nor require users to preserve declaration order.
  std::optional<selector> owner_first;
  std::weak_ptr<int> owner_first_lifetime;
  {
    ntl::net::http::decision_policy temporary;
    owner_first.emplace(temporary.requests().at_headers());
    auto lifetime = std::make_shared<int>(1);
    owner_first_lifetime = lifetime;
    owner_first->when(
        [lifetime = std::move(lifetime)](
            const ntl::net::http::inspection_context_view &) noexcept {
          return *lifetime == 1;
        });
  }
  require(!owner_first_lifetime.expired(),
          "destroying the HTTP policy facade invalidated a live selector");
  owner_first->decide(
      [](const ntl::net::http::inspection_context_view &) noexcept {
        return ntl::net::inspection::verdict::block;
      });
  owner_first.reset();
  require(owner_first_lifetime.expired(),
          "orphan HTTP policy state leaked after its last child completed");
}

void test_repeated_header_and_trailer_conditions() {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http1;
  request.method = "POST";
  request.scheme = "https";
  request.authority = "adapter.example.test";
  request.path = "/inspect";
  request.headers.append("x-policy", "benign-first");
  request.headers.append("x-policy", "inspect");
  request.headers.append("x-list", "one");
  request.headers.append("x-list", "two");
  request.headers.append("Custom-Request-Proof", "yes");
  request.trailers.push_back({"x-request-proof", "complete", false});
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http1;
  response.status = 200;
  response.headers.append("Custom-Response-Proof", "yes");
  response.headers.append("x-result", "one");
  response.headers.append("x-result", "two");
  response.trailers.push_back({"x-response-proof", "complete", false});
  ntl::net::http::inspection_session_metadata session;
  const auto context = ntl::net::http::inspection_context_view::for_response(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::message_complete, session, request,
      response);
  const auto request_context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::message_complete, session, request);
  using namespace ntl::net::http::condition;
  require(request_header_is("x-policy", "inspect")(context) &&
              !unique_request_header_is("x-policy", "inspect")(context) &&
              request_header_count_is("x-policy", 2)(context) &&
              all_request_header_values("x-list", [](std::string_view value) {
                return value == "one" || value == "two";
              })(context) &&
              all_header_values("x-list", [](std::string_view value) {
                return value == "one" || value == "two";
              })(request_context) &&
              response_header_count_is("x-result", 2)(context) &&
              all_response_header_values(
                  "x-result", [](std::string_view value) {
                    return value == "one" || value == "two";
                  })(context) &&
              !response_header_count_is("x-result", 0)(request_context) &&
              !all_response_header_values(
                  "x-result", [](std::string_view) { return true; })(
                  request_context) &&
              request_trailer_is("x-request-proof", "complete")(context) &&
              response_trailer_is("x-response-proof", "complete")(context) &&
              request_header_name_starts_with("CUSTOM-")(context) &&
              response_header_name_starts_with("custom-")(context),
          "repeated-header or typed trailer conditions were inconsistent");

  bool rejected_invalid_prefix = false;
  try {
    (void)header_name_starts_with("custom prefix");
  } catch (const std::invalid_argument &) {
    rejected_invalid_prefix = true;
  }
  require(rejected_invalid_prefix,
          "invalid HTTP header-name prefix was accepted");
}

void test_query_presence_conditions() {
  ntl::net::http::inspection_session_metadata session;
  ntl::net::http::request_message request;
  request.method = "GET";
  request.path = "/inspect";
  const auto context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::headers, session, request);
  using namespace ntl::net::http::condition;

  require(!context.has_query() && context.query().empty() &&
              query_absent()(context) && !query_present()(context) &&
              !query_is("")(context),
          "absent HTTP query matched an explicitly empty query");
  request.path = "/inspect?";
  require(context.has_query() && context.query().empty() &&
              query_present()(context) && !query_absent()(context) &&
              query_is("")(context),
          "explicitly empty HTTP query was not preserved");
  request.path = "/inspect?mode=block";
  require(query_present()(context) && query_is("mode=block")(context) &&
              !query_is("")(context),
          "non-empty HTTP query condition was inconsistent");
}

void test_protocol_neutral_typed_condition_parity() {
  using context_type = ntl::net::http::inspection_context_view;
  static_assert(!std::is_constructible_v<
                context_type, ntl::net::http::protocol, std::uint64_t,
                std::uint64_t, ntl::net::http::message_direction,
                ntl::net::http::inspection_stage,
                const ntl::net::http::inspection_session_metadata &,
                const ntl::net::http::request_message &,
                const ntl::net::http::response_message *,
                std::span<const std::byte>>);

  ntl::net::http::request_message request;
  request.method = "POST";
  request.scheme = "https";
  request.authority = "adapter.example.test";
  request.path = "/inspect?mode=block";
  request.headers.append("Custom-Request-Proof", "enabled");
  ntl::net::http::response_message response;
  response.status = 200;
  response.headers.append("Custom-Response-Proof", "enabled");
  ntl::net::http::inspection_session_metadata session;
  session.connection.process_id = 9001;
  session.connection.original_destination =
      ntl::net::http::endpoint_metadata{"203.0.113.10", 443};
  session.tls.server_name = "adapter.example.test";
  session.tls.alpn = "semantic-test";

  const auto cross_field =
      [](const ntl::net::http::inspection_context_view &context) noexcept {
        return context.method() == "POST" && context.path() == "/inspect" &&
               context.query() == "mode=block" &&
               context.connection().process_id == 9001 &&
               context.tls().server_name == "adapter.example.test";
      };
  ntl::net::http::decision_policy policy;
  using namespace ntl::net::http::condition;
  policy.requests()
      .at_headers()
      .when(header_name_starts_with("CUSTOM-"))
      .when(any_header([](const ntl::net::http::header_field &header) {
        return header.name == "custom-request-proof" &&
               header.value == "enabled";
      }))
      .when(cross_field)
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
  policy.responses()
      .at_headers()
      .when(request_header_name_starts_with("custom-request-"))
      .when(response_header_name_starts_with("custom-response-"))
      .when(any_response_header(
          [](const ntl::net::http::header_field &header) {
            return header.name == "custom-response-proof" &&
                   header.value == "enabled";
          }))
      .when(cross_field)
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });

  constexpr std::array protocols{
      ntl::net::http::protocol::http1,
      ntl::net::http::protocol::http2,
      ntl::net::http::protocol::http3};
  for (const auto protocol : protocols) {
    request.wire_protocol = protocol;
    response.wire_protocol = protocol;
    const auto request_context = context_type::for_request(
        protocol, 7, 11, ntl::net::http::inspection_stage::headers, session,
        request);
    const auto response_context = context_type::for_response(
        protocol, 7, 11, ntl::net::http::inspection_stage::headers, session,
        request, response);
    require(request_context.direction() ==
                    ntl::net::http::message_direction::request &&
                request_context.response() == nullptr &&
                request_context.headers().contains("custom-request-proof") &&
                !response_header_name_starts_with("custom-")(
                    request_context) &&
                response_context.direction() ==
                    ntl::net::http::message_direction::response &&
                response_context.response() == &response &&
                response_context.headers().contains("custom-response-proof") &&
                protocol_is(protocol)(request_context) &&
                protocol_is(protocol)(response_context),
            "inspection context factory mixed request and response state");
    require(policy.evaluate(request_context) ==
                    ntl::net::inspection::verdict::block &&
                policy.evaluate(response_context) ==
                    ntl::net::inspection::verdict::block,
            "typed HTTP condition semantics changed across wire protocols");
  }
}

void test_metadata_presence_and_ownership() {
  ntl::net::http::inspection_session_metadata session;
  ntl::net::http::request_message request;
  request.method = "GET";
  request.path = "/";
  const auto missing = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 1,
      ntl::net::http::inspection_stage::headers, session, request);
  using namespace ntl::net::http::condition;
  require(!process_is(0)(missing) && !connection_is(0)(missing) &&
              !flow_is(0)(missing) &&
              !original_destination_port_is(0)(missing) &&
              !tls_server_name_is("")(missing) && !alpn_is("")(missing) &&
              !application_label_is("")(missing) &&
              !application_id_is({})(missing),
          "absent inspection metadata matched a zero/empty condition");

  {
    std::string temporary_name = "owned.example.test";
    std::string temporary_label = "browser.exe";
    std::vector<std::byte> temporary_id{std::byte{1}, std::byte{2}};
    session.tls.server_name = temporary_name;
    session.tls.alpn = std::string("h2");
    session.connection.application_label = temporary_label;
    session.connection.application_id = temporary_id;
  }
  const auto owned = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http2, 1, 1,
      ntl::net::http::inspection_stage::headers, session, request);
  require(tls_server_name_is("owned.example.test")(owned) &&
              alpn_is("h2")(owned) &&
              application_label_is("browser.exe")(owned) &&
              application_id_is({std::byte{1}, std::byte{2}})(owned),
          "inspection session metadata did not own its backing storage");
}

sample::coroutine_task<ntl::net::http::http1_proxy_result>
run_connect_tunnel(memory_stream &downstream, memory_stream &upstream,
                   bool &observed) {
  auto downstream_owner = std::make_shared<memory_stream>(std::move(downstream));
  auto upstream_owner = std::make_shared<memory_stream>(std::move(upstream));
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  policy->use_content_codecs(decoders, encoders);
  auto connection = std::make_shared<
      ntl::net::http::http1_proxy_connection<memory_stream, memory_stream>>(
      downstream_owner, upstream_owner, policy,
      ntl::net::http::http1_request_target_context{.origin_scheme = "https"});
  struct connect_tunnel_handler {
    bool *observed = nullptr;

    ntl::result<ntl::net::http::http1_tunnel_disposition> admit(
        const ntl::net::http::http1_tunnel_offer_view &offer) const noexcept {
      return ntl::ok(
          offer.kind == ntl::net::http::http1_tunnel_kind::connect
              ? ntl::net::http::http1_tunnel_disposition::inspect
              : ntl::net::http::http1_tunnel_disposition::reject);
    }

    ntl::net::user::task<void> operator()(
        ntl::net::http::http1_upgrade_context_view<
            memory_stream, memory_stream> &context) const {
      std::string carry = text_of(context.upstream_carry);
      std::array<std::byte, 32> buffer{};
      constexpr std::string_view expected = "origin-tunnel-carry";
      while (carry.size() < expected.size()) {
        const std::size_t received = co_await context.upstream.read_some_borrowed(
            std::span(buffer).first(expected.size() - carry.size()));
        if (received == 0)
          break;
        carry.append(reinterpret_cast<const char *>(buffer.data()), received);
      }
      *observed =
          context.kind == ntl::net::http::http1_tunnel_kind::connect &&
          context.disposition ==
              ntl::net::http::http1_tunnel_disposition::inspect &&
          carry == expected;
      co_return;
    }
  };
  const auto result = co_await connection->run(
      ntl::net::http::null_http1_observer{},
      connect_tunnel_handler{&observed});
  downstream = std::move(*downstream_owner);
  upstream = std::move(*upstream_owner);
  co_return result;
}

sample::coroutine_task<ntl::net::http::http1_proxy_result>
run_authority_binding_case(
    memory_stream &downstream, memory_stream &upstream,
    std::optional<std::string> tls_server_name, bool require_binding) {
  auto downstream_owner = std::make_shared<memory_stream>(std::move(downstream));
  auto upstream_owner = std::make_shared<memory_stream>(std::move(upstream));
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  policy->use_content_codecs(decoders, encoders);
  ntl::net::http::http1_proxy_limits limits;
  limits.require_server_name_authority_binding = require_binding;
  ntl::net::http::inspection_session_metadata metadata;
  metadata.tls.server_name = std::move(tls_server_name);
  metadata.tls.alpn = "http/1.1";
  auto connection = std::make_shared<
      ntl::net::http::http1_proxy_connection<memory_stream, memory_stream>>(
      downstream_owner, upstream_owner, policy,
      ntl::net::http::http1_request_target_context{.origin_scheme = "https"},
      limits, std::move(metadata));
  auto unexpected_tunnel = [](auto &) -> ntl::net::user::task<void> {
    throw std::runtime_error("unexpected HTTP/1 tunnel");
    co_return;
  };
  const auto result = co_await connection->run(
      ntl::net::http::null_http1_observer{}, unexpected_tunnel);
  downstream = std::move(*downstream_owner);
  upstream = std::move(*upstream_owner);
  co_return result;
}

sample::coroutine_task<ntl::net::http::http1_proxy_result>
run_default_rejected_connect(
    memory_stream &downstream, memory_stream &upstream) {
  auto downstream_owner = std::make_shared<memory_stream>(std::move(downstream));
  auto upstream_owner = std::make_shared<memory_stream>(std::move(upstream));
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  auto policy = std::make_shared<ntl::net::http::inspection_policy>();
  policy->use_content_codecs(decoders, encoders);
  auto connection = std::make_shared<
      ntl::net::http::http1_proxy_connection<memory_stream, memory_stream>>(
      downstream_owner, upstream_owner, policy,
      ntl::net::http::http1_request_target_context{.origin_scheme = "https"});
  auto no_admission = [](auto &) -> ntl::net::user::task<void> {
    throw std::runtime_error("unadmitted HTTP/1 tunnel reached relay");
    co_return;
  };
  const auto result = co_await connection->run(
      ntl::net::http::null_http1_observer{}, no_admission);
  downstream = std::move(*downstream_owner);
  upstream = std::move(*upstream_owner);
  co_return result;
}

void test_connect_tunnel_and_expect_fail_closed() {
  {
    memory_stream downstream(
        "CONNECT service.example.test:443 HTTP/1.1\r\n"
        "Host: service.example.test:443\r\n\r\n");
    memory_stream upstream(
        "HTTP/1.1 200 Connection Established\r\n\r\n"
        "origin-tunnel-carry");
    bool observed = false;
    const auto result =
        run_connect_tunnel(downstream, upstream, observed).get();
    if (!(observed &&
          result.termination ==
              ntl::net::http::http1_proxy_termination::upgraded &&
          result.last_status == 200))
      throw std::runtime_error(
          "HTTP/1 successful CONNECT tunnel evidence mismatch: observed=" +
          std::to_string(observed) + ", termination=" +
          std::to_string(static_cast<unsigned>(result.termination)) +
          ", status=" + std::to_string(result.last_status));
  }

  {
    memory_stream downstream(
        "CONNECT blocked.example.test:443 HTTP/1.1\r\n"
        "Host: blocked.example.test:443\r\n\r\n");
    memory_stream upstream(
        "HTTP/1.1 200 Connection Established\r\n\r\n");
    const auto result =
        run_default_rejected_connect(downstream, upstream).get();
    require(
        result.termination ==
                ntl::net::http::http1_proxy_termination::terminal_response &&
            result.last_status == 403 && upstream.written_text().empty() &&
            downstream.written_text().starts_with("HTTP/1.1 403"),
        "HTTP/1 default tunnel rejection leaked CONNECT to the origin");
  }

  {
    memory_stream downstream(
        "GET / HTTP/1.1\r\nHost: bound.example.test\r\n\r\n");
    memory_stream upstream(
        "HTTP/1.1 204 No Content\r\n\r\n");
    const auto result = run_authority_binding_case(
                            downstream, upstream, "bound.example.test", true)
                            .get();
    require(result.completed_exchanges == 1 &&
                upstream.written_text().starts_with("GET /"),
            "HTTP/1 strict SNI binding rejected a matching authority");
  }

  {
    memory_stream downstream(
        "GET / HTTP/1.1\r\nHost: other.example.test\r\n\r\n");
    memory_stream upstream(
        "HTTP/1.1 204 No Content\r\n\r\n");
    const auto result = run_authority_binding_case(
                            downstream, upstream, "bound.example.test", true)
                            .get();
    require(
        result.termination ==
                ntl::net::http::http1_proxy_termination::terminal_response &&
            result.last_status == 403 && upstream.written_text().empty(),
        "HTTP/1 strict SNI binding leaked a mismatched authority");
  }

  {
    memory_stream downstream(
        "GET / HTTP/1.1\r\nHost: coalesced.example.test\r\n\r\n");
    memory_stream upstream(
        "HTTP/1.1 204 No Content\r\n\r\n");
    const auto result = run_authority_binding_case(
                            downstream, upstream, "bound.example.test", false)
                            .get();
    require(result.completed_exchanges == 1 &&
                upstream.written_text().starts_with("GET /"),
            "HTTP/1 explicit coalescing opt-out did not bypass strict SNI binding");
  }

  {
    const std::string request_head =
        "POST /upload HTTP/1.1\r\nHost: adapter.example.test\r\n"
        "Content-Length: 7\r\nExpect: 100-continue\r\n\r\n";
    const auto expectation =
        ntl::net::http::inspect_http1_request_expectation(
            ntl::net::scatter_view::from_contiguous(
                std::as_bytes(std::span(request_head))));
    require(expectation ==
                ntl::net::http::http1_request_expectation::continue_100,
            "HTTP/1 Expect: 100-continue was not recognized at the header boundary");

    auto downstream = std::make_shared<continue_gated_stream>(
        request_head, "payload");
    auto upstream = std::make_shared<memory_stream>(
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    auto policy = std::make_shared<ntl::net::http::inspection_policy>();
    policy->transforms_ref().requests().transform(
        [](ntl::net::http::request_message &request) {
          request.headers.set("x-after-continue", "yes");
          return ntl::net::http::rewrite_result::headers_changed();
        });
    const auto result = run_memory_proxy(
                            downstream, upstream, policy, decoders, encoders,
                            {}, ntl::net::http::null_http1_observer{})
                            .get();
    const std::string origin_wire = upstream->written_text();
    require(downstream->continue_count() == 1 &&
                downstream->written_text().starts_with(
                    "HTTP/1.1 100 Continue\r\n\r\n") &&
                downstream->written_text().find("HTTP/1.1 200 OK") !=
                    std::string::npos &&
                result.completed_exchanges == 1 && result.last_status == 200,
            "HTTP/1 bounded local continue did not reach the final response");
    require(origin_wire.find("x-after-continue: yes") != std::string::npos &&
                origin_wire.ends_with("payload") &&
                origin_wire.find("POST ") == origin_wire.rfind("POST "),
            "HTTP/1 continue path leaked or duplicated the pre-transform request");
  }

  {
    const std::string first_head =
        "POST /first HTTP/1.1\r\nHost: adapter.example.test\r\n"
        "Content-Length: 3\r\nExpect: 100-continue\r\n\r\n";
    const std::string second_head =
        "POST /second HTTP/1.1\r\nHost: adapter.example.test\r\n"
        "Content-Length: 3\r\nExpect: 100-continue\r\n\r\n";
    auto downstream = std::make_shared<persistent_continue_gated_stream>(
        first_head, "one", second_head, "two");
    auto upstream = std::make_shared<memory_stream>(
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n1"
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n2");
    auto decoders = std::make_shared<
        ntl::net::inspection::content_decoder_registry>();
    auto encoders = std::make_shared<
        ntl::net::inspection::content_encoder_registry>();
    auto policy = std::make_shared<ntl::net::http::inspection_policy>();
    const auto result = run_memory_proxy(
                            downstream, upstream, policy, decoders, encoders,
                            {}, ntl::net::http::null_http1_observer{})
                            .get();
    const std::string origin_wire = upstream->written_text();
    require(downstream->continue_count() == 2 &&
                result.completed_exchanges == 2 && result.last_status == 200 &&
                origin_wire.find("POST /first") != std::string::npos &&
                origin_wire.find("POST /second") != std::string::npos &&
                origin_wire.find("one") != std::string::npos &&
                origin_wire.find("two") != std::string::npos,
            "HTTP/1 persistent connection did not reset Expect state per request");
  }
}

struct tls_fixture {
  sample::ephemeral_certificate authority{false};
  ntl::net::windows_tls_certificate_issuer origin_issuer{
      authority.get(), {.key_name_prefix = L"crtsys-http1-origin",
                        .rsa_bits = 2048, .validity_days = 1,
                        .machine_keys = false}};
  ntl::net::windows_tls_certificate_issuer proxy_issuer{
      authority.get(), {.key_name_prefix = L"crtsys-http1-proxy",
                        .rsa_bits = 2048, .validity_days = 1,
                        .machine_keys = false}};
  std::shared_ptr<ntl::net::tls_server_identity> origin_identity =
      std::make_shared<ntl::net::tls_server_identity>(
          origin_issuer.issue(server_name));
  std::shared_ptr<ntl::net::tls_server_identity> proxy_identity =
      std::make_shared<ntl::net::tls_server_identity>(
          proxy_issuer.issue(server_name));
  ntl::net::tls_credentials client_credentials =
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
  ntl::net::async_socket proxy_inbound(context,
                                       proxy_inbound_native.release());
  ntl::net::async_socket proxy_outbound(context,
                                        proxy_outbound_native.release());
  ntl::net::async_socket origin_socket(context, origin_native.release());
  ntl::net::tls_stream browser_tls(browser_socket,
                                   certificates.client_credentials);
  const ntl::net::tls_stream_limits proxy_tls_limits{
      .receive_chunk_size = 257};
  auto proxy_inbound_tls = std::make_shared<ntl::net::tls_stream>(
      proxy_inbound, certificates.proxy_identity->credentials(),
      proxy_tls_limits);
  auto proxy_outbound_tls = std::make_shared<ntl::net::tls_stream>(
      proxy_outbound, certificates.client_credentials, proxy_tls_limits);
  ntl::net::tls_stream origin_tls(
      origin_socket, certificates.origin_identity->credentials());

  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::inspection::register_standard_content_decoders(*decoders);
  ntl::net::inspection::register_standard_content_encoders(*encoders);
  auto pipeline = std::make_shared<ntl::net::http::transform_pipeline>(
      make_pipeline());
  auto origin = start_origin(origin_tls);
  auto proxy = run_http1_proxy(
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
  try {
    proxy_result.emplace(proxy.get());
  } catch (...) {
    proxy_failure = std::current_exception();
    stop_all();
  }
  try {
    client_result.emplace(client.get());
  } catch (...) {
    client_failure = std::current_exception();
    stop_all();
  }
  try {
    origin_result.emplace(origin.get());
  } catch (...) {
    origin_failure = std::current_exception();
  }
  context.wait_for_idle();
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
        "HTTP/1 exchange failed: proxy=" + describe(proxy_failure) +
        "; client=" + describe(client_failure) +
        "; origin=" + describe(origin_failure));
  }
  return std::tuple(std::move(*client_result), std::move(*proxy_result),
                    std::move(*origin_result));
}

void test_persistent_adapter() {
  temporary_directory output;
  auto logger =
      std::make_shared<browser::browser_html_logger>(output.get());
  tls_fixture certificates;
  const auto [client, proxy, origin] = run_exchange(
      certificates, logger,
      [](ntl::net::tls_stream &stream) { return run_persistent_origin(stream); },
      [](ntl::net::tls_stream &stream,
         std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy) {
        return run_persistent_client(stream, policy);
      });
  require(origin.transformed_header, "HTTP/1 adapter request transform was bypassed");
  require(origin.paths == std::vector<std::string>{
                              "/head", "/hints", "/chunked", "/no-content",
                              "/not-modified", "/close"},
          "HTTP/1 adapter lost or reordered pipelined requests");
  require(client.statuses == std::vector<unsigned>{200, 200, 200, 204, 304, 200},
          "HTTP/1 adapter returned wrong persistent response statuses");
  require(client.informational == 1,
          "HTTP/1 adapter did not preserve exactly one informational response");
  require(client.bodies[0].empty(), "HEAD response exposed a wire body");
  require(client.bodies[1].find("ntl-http1-adapter") != std::string::npos &&
              client.bodies[2].find("chunked") != std::string::npos &&
              client.bodies[2].find("ntl-http1-adapter") != std::string::npos &&
              client.bodies[3].empty() && client.bodies[4].empty() &&
              client.bodies[5] == "close-delimited-body",
          "HTTP/1 adapter body framing or transformation evidence is incomplete");
  require(proxy.status == 200,
          "HTTP/1 adapter did not report the final persistent response");
}

void test_websocket_carry_over() {
  temporary_directory output;
  auto logger =
      std::make_shared<browser::browser_html_logger>(output.get());
  tls_fixture certificates;
  const auto [client, proxy, origin] = run_exchange(
      certificates, logger,
      [](ntl::net::tls_stream &stream) { return run_websocket_origin(stream); },
      [](ntl::net::tls_stream &stream,
         std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy) {
        return run_websocket_client(stream, policy);
      });
  require(client.text == "origin-carry" && client.close,
          "HTTP/1 adapter lost origin WebSocket carry-over bytes");
  require(origin.text == "client-carry" && origin.close,
          "HTTP/1 adapter lost browser WebSocket carry-over bytes");
  require(proxy.status == 101,
          "HTTP/1 adapter did not report the WebSocket upgrade");
}

} // namespace

int main() {
  try {
    sample::winsock_session winsock;
    const auto run = [](std::string_view name, auto test) {
      try {
        test();
      } catch (const std::exception &error) {
        throw std::runtime_error(std::string(name) + ": " + error.what());
      }
    };
    run("staged-context", test_full_staged_context_policy);
    run("owner-first-close", test_http1_close_lifetime_contract);
    run("terminal-and-empty-body", test_terminal_transform_and_empty_body_stages);
    run("throwing-observer", test_throwing_observer_cannot_change_forwarding);
    run("invalid-verdict", test_invalid_verdict_obeys_failure_policy);
    run("first-match-policy", test_first_match_permit_and_catch_all_block);
    run("rule-builder-lifetime", test_rule_builder_lifetime_and_empty_decision);
    run("header-trailer-conditions",
        test_repeated_header_and_trailer_conditions);
    run("query-presence", test_query_presence_conditions);
    run("protocol-neutral-conditions",
        test_protocol_neutral_typed_condition_parity);
    run("metadata-presence-ownership", test_metadata_presence_and_ownership);
    run("connect-expect", test_connect_tunnel_and_expect_fail_closed);
    run("persistent", test_persistent_adapter);
    run("websocket", test_websocket_carry_over);
    std::cout << "HTTP/1 actual adapter contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
