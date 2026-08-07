std::vector<std::byte> encode_websocket_text(
    std::string_view text, ntl::net::websocket::sender_role sender,
    const ntl::net::websocket::permessage_deflate_parameters &parameters) {
  auto encoder = ntl::net::websocket::make_permessage_deflate_encoder(
      parameters, sender, 64 * 1024, 64 * 1024);
  auto compressed = encoder.encode(std::as_bytes(std::span(text)));
  if (!compressed)
    throw std::runtime_error("managed WebSocket compression failed");
  const auto mask = [] {
    return std::array<std::byte, 4>{std::byte{0x11}, std::byte{0x22},
                                    std::byte{0x33}, std::byte{0x44}};
  };
  auto wire = ntl::net::websocket::transform_detail::encode_frame(
      ntl::net::websocket::opcode::text, true, 0x04, *compressed, sender,
      sender == ntl::net::websocket::sender_role::client
          ? std::function<std::array<std::byte, 4>()>(mask)
          : std::function<std::array<std::byte, 4>()>{});
  if (!wire)
    throw std::runtime_error("managed WebSocket frame encoding failed");
  return std::move(*wire);
}

std::vector<std::byte> encode_websocket_close(
    ntl::net::websocket::sender_role sender) {
  const auto mask = [] {
    return std::array<std::byte, 4>{std::byte{0x55}, std::byte{0x66},
                                    std::byte{0x77}, std::byte{0x88}};
  };
  auto wire = ntl::net::websocket::transform_detail::encode_frame(
      ntl::net::websocket::opcode::close, true, 0, {}, sender,
      sender == ntl::net::websocket::sender_role::client
          ? std::function<std::array<std::byte, 4>()>(mask)
          : std::function<std::array<std::byte, 4>()>{});
  if (!wire)
    throw std::runtime_error("managed WebSocket close encoding failed");
  return std::move(*wire);
}

struct websocket_wire {
  std::vector<std::byte> frame;
  std::vector<std::byte> carry;
};

ntl::net::user::task<websocket_wire> read_websocket_frame(
    ntl::net::tls_stream &stream,
    ntl::net::websocket::sender_role sender,
    std::vector<std::byte> carry = {}) {
  ntl::net::tls_framed_stream framed(
      stream,
      ntl::net::websocket::frame_framer(
          sender, {.maximum_payload_size = 64 * 1024,
                   .allowed_reserved_bits = 0x04}),
      {64 * 1024 + 14}, 1024);
  framed.append_buffered(carry);
  auto message = co_await framed.read_frame_or_eof();
  if (!message)
    throw std::runtime_error("managed WebSocket stream ended early");
  websocket_wire result;
  result.frame.assign(message->frame().begin(), message->frame().end());
  result.carry = framed.release_buffered();
  co_return result;
}

std::string decode_websocket_text(
    std::span<const std::byte> wire,
    ntl::net::websocket::sender_role sender,
    const ntl::net::websocket::permessage_deflate_parameters &parameters) {
  const auto view = ntl::net::scatter_view::from_contiguous(wire);
  auto header = ntl::net::websocket::inspect_header(
      view, sender, {.maximum_payload_size = 64 * 1024,
                     .allowed_reserved_bits = 0x04});
  if (!header || header->operation != ntl::net::websocket::opcode::text ||
      (header->reserved_bits & 0x04u) == 0)
    throw std::runtime_error("managed WebSocket text header is invalid");
  auto payload = ntl::net::websocket::decode_payload(view, *header,
                                                     64 * 1024);
  if (!payload)
    throw std::runtime_error("managed WebSocket payload decoding failed");
  auto decoder = ntl::net::websocket::make_permessage_deflate_decoder(
      parameters, sender, 64 * 1024);
  auto plain = decoder.decode(*payload, true);
  if (!plain)
    throw std::runtime_error("managed WebSocket decompression failed");
  return text_of(*plain);
}

bool is_websocket_close(std::span<const std::byte> wire,
                        ntl::net::websocket::sender_role sender) {
  const auto view = ntl::net::scatter_view::from_contiguous(wire);
  auto header = ntl::net::websocket::inspect_header(
      view, sender, {.maximum_payload_size = 64 * 1024});
  return header && header->operation == ntl::net::websocket::opcode::close;
}

struct websocket_exchange_result {
  bool mutual_tls = false;
  bool request_transformed = false;
  bool client_message_transformed = false;
  bool server_message_transformed = false;
  bool compressed = false;
  bool closed = false;
};

ntl::net::user::task<websocket_exchange_result> serve_websocket_origin(
    ntl::net::tls_stream &stream, PCCERT_CONTEXT client_certificate) {
  auto client_policy =
      std::make_shared<ntl::net::exact_client_certificate_policy>(
          client_certificate);
  co_await stream.handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true,
       .require_client_certificate = true,
       .client_certificate_policy = std::move(client_policy)});
  ntl::net::tls_framed_stream requests(
      stream, make_http_framer(ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 251);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error("managed WebSocket origin received no upgrade");
  const std::string request_text = text_of(request->frame());
  if (!ascii_contains_ci(request_text, "upgrade: websocket") ||
      !ascii_contains_ci(request_text, "x-ntl-inspected: 1\r\n"))
    throw std::runtime_error("managed WebSocket upgrade was not transformed");
  constexpr std::string_view extension =
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover";
  auto parameters =
      ntl::net::websocket::parse_permessage_deflate_response(extension);
  if (!parameters)
    throw std::runtime_error("managed WebSocket extension is invalid");
  const std::string response =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\n"
      "Upgrade: websocket\r\nSec-WebSocket-Extensions: " +
      std::string(extension) + "\r\n\r\n";
  if (co_await stream.write_all(std::as_bytes(std::span(response))) !=
      response.size())
    throw std::runtime_error("managed WebSocket upgrade response was short");

  auto client_text = co_await read_websocket_frame(
      stream, ntl::net::websocket::sender_role::client,
      requests.release_buffered());
  const std::string received = decode_websocket_text(
      client_text.frame, ntl::net::websocket::sender_role::client,
      *parameters);
  const auto reply = encode_websocket_text(
      "server-message", ntl::net::websocket::sender_role::server,
      *parameters);
  if (co_await stream.write_all(reply) != reply.size())
    throw std::runtime_error("managed WebSocket origin text was short");
  auto close = co_await read_websocket_frame(
      stream, ntl::net::websocket::sender_role::client,
      std::move(client_text.carry));
  if (!is_websocket_close(close.frame,
                          ntl::net::websocket::sender_role::client))
    throw std::runtime_error("managed WebSocket origin received no close");
  const auto close_reply =
      encode_websocket_close(ntl::net::websocket::sender_role::server);
  if (co_await stream.write_all(close_reply) != close_reply.size())
    throw std::runtime_error("managed WebSocket origin close was short");
  co_await close_tls(stream);
  co_return websocket_exchange_result{
      true, true,
      received == "client-message [ntl-kernel]", false, true, true};
}

std::future<websocket_exchange_result> start_websocket_origin(
    const listener &origin, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate) {
  return std::async(std::launch::async,
                    [&origin, origin_certificate, client_certificate]() {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(origin.socket.get(), &readable);
    timeval timeout{10, 0};
    if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
      throw std::runtime_error("managed WebSocket origin accept timed out");
    auto native = accept_one(origin);
    const SOCKET accepted = native.get();
    std::atomic<bool> completed{false};
    std::jthread watchdog([&](std::stop_token stop) {
      for (unsigned count = 0; count != 200 && !stop.stop_requested();
           ++count) {
        if (completed.load(std::memory_order_acquire))
          return;
        std::this_thread::sleep_for(50ms);
      }
      if (!completed.load(std::memory_order_acquire))
        (void)::shutdown(accepted, SD_BOTH);
    });
    ntl::net::io_completion_context context;
    ntl::net::async_socket socket(context, native.release());
    auto credentials = ntl::net::tls_credentials::server(origin_certificate);
    ntl::net::tls_stream stream(socket, credentials);
    try {
      auto operation = serve_websocket_origin(stream, client_certificate);
      auto result = ntl::net::user::sync_wait(std::move(operation));
      completed.store(true, std::memory_order_release);
      watchdog.request_stop();
      context.wait_for_idle();
      return result;
    } catch (...) {
      completed.store(true, std::memory_order_release);
      watchdog.request_stop();
      (void)::shutdown(accepted, SD_BOTH);
      context.wait_for_idle();
      throw;
    }
  });
}

ntl::net::user::task<websocket_exchange_result> run_websocket_client(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(managed_server_name),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  constexpr std::string_view extension =
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover";
  const std::string request =
      "GET /socket HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\n"
      "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: a2VybmVsLWJyb3dzZXI=\r\n"
      "Sec-WebSocket-Extensions: " +
      std::string(extension) + "\r\n\r\n";
  if (co_await stream.write_all(std::as_bytes(std::span(request))) !=
      request.size())
    throw std::runtime_error("managed WebSocket upgrade write was short");
  ntl::net::tls_framed_stream replies(
      stream, make_http_framer(ntl::net::http::http1_message_kind::response),
      {maximum_http_message_size}, 263);
  auto response = co_await replies.read_frame_or_eof();
  if (!response)
    throw std::runtime_error("managed WebSocket proxy returned no upgrade");
  const auto parsed = parse_http_response(*response);
  if (!parsed.websocket_upgrade())
    throw std::runtime_error("managed WebSocket upgrade was rejected");
  auto parameters = ntl::net::websocket::parse_permessage_deflate_response(
      parsed.websocket_extensions);
  if (!parameters)
    throw std::runtime_error("managed WebSocket compression not negotiated");
  const auto message = encode_websocket_text(
      "client-message", ntl::net::websocket::sender_role::client,
      *parameters);
  if (co_await stream.write_all(message) != message.size())
    throw std::runtime_error("managed WebSocket client text was short");
  auto server_text = co_await read_websocket_frame(
      stream, ntl::net::websocket::sender_role::server,
      replies.release_buffered());
  const std::string received = decode_websocket_text(
      server_text.frame, ntl::net::websocket::sender_role::server,
      *parameters);
  const auto close =
      encode_websocket_close(ntl::net::websocket::sender_role::client);
  if (co_await stream.write_all(close) != close.size())
    throw std::runtime_error("managed WebSocket client close was short");
  auto server_close = co_await read_websocket_frame(
      stream, ntl::net::websocket::sender_role::server,
      std::move(server_text.carry));
  const bool closed = is_websocket_close(
      server_close.frame, ntl::net::websocket::sender_role::server);
  co_await close_tls(stream);
  co_return websocket_exchange_result{
      false, false, false,
      received == "server-message [ntl-kernel]", true, closed};
}

websocket_exchange_result exchange_websocket(int family,
                                              std::uint16_t port) {
  auto native = connect_loopback(family, port);
  const SOCKET connected = native.get();
  std::atomic<bool> completed{false};
  std::jthread watchdog([&](std::stop_token stop) {
    for (unsigned count = 0; count != 300 && !stop.stop_requested();
         ++count) {
      if (completed.load(std::memory_order_acquire))
        return;
      std::this_thread::sleep_for(50ms);
    }
    if (!completed.load(std::memory_order_acquire))
      (void)::shutdown(connected, SD_BOTH);
  });
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  auto credentials = ntl::net::tls_credentials::client();
  ntl::net::tls_stream stream(socket, credentials);
  try {
    auto operation = run_websocket_client(stream);
    auto result = ntl::net::user::sync_wait(std::move(operation));
    completed.store(true, std::memory_order_release);
    watchdog.request_stop();
    context.wait_for_idle();
    return result;
  } catch (...) {
    completed.store(true, std::memory_order_release);
    watchdog.request_stop();
    (void)::shutdown(connected, SD_BOTH);
    context.wait_for_idle();
    throw;
  }
}

constexpr std::string_view h2_preface =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
