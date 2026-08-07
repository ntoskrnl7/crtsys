ntl::net::user::task<unsigned> read_exactly(ntl::net::tls_stream &stream,
                                      std::span<std::byte> output) {
  std::size_t used = 0;
  while (used != output.size()) {
    const std::size_t count = co_await stream.read_some_borrowed(output.subspan(used));
    if (count == 0)
      throw std::runtime_error("managed HTTP/2 TLS stream ended early");
    used += count;
  }
  co_return 0;
}

struct h2_wire_frame {
  std::vector<std::byte> bytes;
  ntl::net::http2::frame_header header{};
};

ntl::net::user::task<h2_wire_frame> read_h2_frame(ntl::net::tls_stream &stream) {
  std::array<std::byte, ntl::net::http2::frame_header_size> header_bytes{};
  co_await read_exactly(stream, header_bytes);
  auto header = ntl::net::http2::inspect_header(
      ntl::net::scatter_view::from_contiguous(header_bytes),
      {1024 * 1024, false});
  if (!header)
    throw std::runtime_error("managed HTTP/2 frame header is invalid");
  h2_wire_frame result;
  result.header = *header;
  result.bytes.resize(ntl::net::http2::frame_header_size +
                      header->payload_size);
  std::memcpy(result.bytes.data(), header_bytes.data(), header_bytes.size());
  if (header->payload_size != 0)
    co_await read_exactly(
        stream, std::span(result.bytes).subspan(
                    ntl::net::http2::frame_header_size));
  co_return result;
}

ntl::net::user::task<h2_wire_frame> read_h2_frame_at(
    ntl::net::tls_stream &stream, std::string_view stage) {
  try {
    co_return co_await read_h2_frame(stream);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(stage) + ": " + error.what());
  }
}

std::vector<std::byte> make_h2_frame(
    ntl::net::http2::frame_type type, std::uint8_t flags,
    std::uint32_t stream_id, std::span<const std::byte> payload = {}) {
  auto frame = ntl::net::http2::transform_detail::make_frame(
      type, flags, stream_id, payload);
  if (!frame)
    throw std::runtime_error("managed HTTP/2 frame encoding failed");
  return std::move(frame->wire);
}

std::vector<std::byte> make_h2_settings(bool enable_connect,
                                        std::uint32_t initial_window = 65535) {
  std::vector<std::byte> payload;
  const auto append = [&payload](std::uint16_t identifier,
                                 std::uint32_t value) {
    payload.push_back(static_cast<std::byte>((identifier >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(identifier & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 24) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 16) & 0xffu));
    payload.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
    payload.push_back(static_cast<std::byte>(value & 0xffu));
  };
  append(0x4u, initial_window);
  if (enable_connect)
    append(0x8u, 1);
  return make_h2_frame(ntl::net::http2::frame_type::settings, 0, 0,
                       payload);
}

std::vector<std::byte> make_h2_settings_ack() {
  return make_h2_frame(ntl::net::http2::frame_type::settings, 0x01u, 0);
}

std::vector<std::byte> make_h2_goaway(std::uint32_t last_stream) {
  const std::array<std::byte, 8> payload{
      static_cast<std::byte>((last_stream >> 24) & 0x7fu),
      static_cast<std::byte>((last_stream >> 16) & 0xffu),
      static_cast<std::byte>((last_stream >> 8) & 0xffu),
      static_cast<std::byte>(last_stream & 0xffu), std::byte{}, std::byte{},
      std::byte{}, std::byte{}};
  return make_h2_frame(ntl::net::http2::frame_type::goaway, 0, 0, payload);
}

ntl::net::user::task<unsigned> write_h2_frames(
    ntl::net::tls_stream &stream,
    const std::vector<ntl::net::http2::outbound_frame> &frames) {
  for (const auto &frame : frames) {
    if (co_await stream.write_all(frame.wire) != frame.wire.size())
      throw std::runtime_error("managed HTTP/2 frame write was short");
  }
  co_return 0;
}

std::vector<std::byte> h2_data_payload(
    const ntl::net::http2::frame_view &frame) {
  auto view = frame.data_payload();
  if (!view)
    throw std::runtime_error("managed HTTP/2 DATA payload is invalid");
  std::vector<std::byte> result(view->size());
  if (!view->copy_to(result).is_ok())
    throw std::runtime_error("managed HTTP/2 DATA payload copy failed");
  return result;
}

ntl::net::user::task<h2_wire_frame> read_h2_data_frame(
    ntl::net::tls_stream &stream, std::uint32_t stream_id) {
  for (unsigned count = 0; count != 256; ++count) {
    auto wire = co_await read_h2_frame(stream);
    if (wire.header.type == ntl::net::http2::frame_type::settings) {
      if (!wire.header.acknowledgement()) {
        const auto acknowledgement = make_h2_settings_ack();
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("managed HTTP/2 SETTINGS ack was short");
      }
      continue;
    }
    if (wire.header.type == ntl::net::http2::frame_type::window_update)
      continue;
    if (wire.header.type == ntl::net::http2::frame_type::data &&
        wire.header.stream_id == stream_id)
      co_return wire;
    throw std::runtime_error("managed HTTP/2 received an unexpected frame");
  }
  throw std::runtime_error("managed HTTP/2 DATA frame timed out");
}

ntl::net::http::request_message make_h2_request(std::string path,
                                                bool block = false) {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = "GET";
  request.scheme = "https";
  request.authority = std::string(managed_server_name_ascii);
  request.path = std::move(path);
  if (block)
    request.headers.set("x-ntl-block", "1");
  return request;
}

ntl::net::http::request_message make_h2_websocket_request() {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = "CONNECT";
  request.scheme = "https";
  request.authority = std::string(managed_server_name_ascii);
  request.path = "/socket";
  request.extended_protocol = "websocket";
  request.headers.append("origin", "https://localhost");
  request.headers.append(
      "sec-websocket-extensions",
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover");
  return request;
}

ntl::net::http::request_message make_h2_grpc_request(
    std::span<const std::byte> body) {
  auto request = make_h2_request("/grpc");
  request.method = "POST";
  request.headers.append("content-type", "application/grpc");
  request.headers.append("content-length", std::to_string(body.size()));
  request.body.assign(body.begin(), body.end());
  return request;
}

ntl::net::http::request_message make_h2_unsupported_connect_request() {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = "CONNECT";
  request.scheme = "https";
  request.authority = std::string(managed_server_name_ascii);
  request.path = "/webtransport";
  request.extended_protocol = "webtransport";
  return request;
}

struct http2_origin_result {
  bool mutual_tls = false;
  bool transformed_headers = true;
  bool multiplexed = false;
  bool extended_connect = false;
  bool websocket_transformed = false;
  bool grpc_request_transformed = false;
  bool goaway = false;
  std::vector<std::uint32_t> received_streams;
  std::vector<std::uint32_t> response_streams;
};

ntl::net::user::task<http2_origin_result> serve_http2_origin(
    ntl::net::tls_stream &stream, PCCERT_CONTEXT client_certificate) {
  auto client_policy =
      std::make_shared<ntl::net::exact_client_certificate_policy>(
          client_certificate);
  co_await stream.handshake_server(
      {.application_protocols = {"h2"},
       .require_application_protocol = true,
       .require_client_certificate = true,
       .client_certificate_policy = std::move(client_policy)});
  std::array<std::byte, h2_preface.size()> preface{};
  try {
    co_await read_exactly(stream, preface);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("origin preface: ") + error.what());
  }
  if (std::memcmp(preface.data(), h2_preface.data(), preface.size()) != 0)
    throw std::runtime_error("managed HTTP/2 preface is invalid");
  const auto settings = make_h2_settings(true);
  if (co_await stream.write_all(settings) != settings.size())
    throw std::runtime_error("managed HTTP/2 origin SETTINGS was short");

  ntl::net::http::transform_pipeline pipeline;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  exchanges->peer_extended_connect_enabled(true);
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests, exchanges, pipeline,
      decoders, encoders);
  http2_origin_result result{};
  result.mutual_tls = true;
  bool extended_open = false;
  while (result.received_streams.size() != 4 || !extended_open) {
    auto wire = co_await read_h2_frame_at(stream, "origin request frame");
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(wire.bytes),
        {1024 * 1024, false});
    if (!frame)
      throw std::runtime_error("managed HTTP/2 origin frame is invalid");
    if (frame->header().type == ntl::net::http2::frame_type::settings) {
      if (!frame->header().acknowledgement()) {
        const auto acknowledgement = make_h2_settings_ack();
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("managed HTTP/2 SETTINGS ack was short");
      }
      continue;
    }
    if (frame->header().type == ntl::net::http2::frame_type::window_update)
      continue;
    auto transformed = requests.consume(*frame);
    if (!transformed)
      throw std::runtime_error("managed HTTP/2 origin request parse failed");
    if (!transformed->message_complete || !transformed->request)
      continue;
    const auto &request = *transformed->request;
    if (request.headers.first("x-ntl-inspected") != "1")
      result.transformed_headers = false;
    if (request.extended_protocol) {
      if (*request.extended_protocol != "websocket" ||
          transformed->stream_id != 9)
        throw std::runtime_error("managed HTTP/2 extended CONNECT is invalid");
      if (!exchanges->admit_connect(
               transformed->stream_id,
               ntl::net::http2::connect_disposition::inspect)
               .is_ok())
        throw std::runtime_error(
            "managed HTTP/2 extended CONNECT admission failed");
      ntl::net::http::response_message accepted;
      accepted.wire_protocol = ntl::net::http::protocol::http2;
      accepted.status = 200;
      accepted.headers.append(
          "sec-websocket-extensions",
          "permessage-deflate; client_no_context_takeover; "
          "server_no_context_takeover");
      auto opening = ntl::net::http2::encode_response_frames(
          9, accepted, {}, ntl::net::http2::default_maximum_frame_size,
          256 * 1024, false, false);
      if (!opening)
        throw std::runtime_error(
            "managed HTTP/2 extended response encoding failed");
      co_await write_h2_frames(stream, *opening);
      extended_open = true;
      result.extended_connect = true;
      continue;
    }
    if (transformed->stream_id != 1 && transformed->stream_id != 5 &&
        transformed->stream_id != 7 && transformed->stream_id != 11)
      throw std::runtime_error("blocked HTTP/2 request reached the origin");
    if (transformed->stream_id == 11) {
      result.grpc_request_transformed =
          request.headers.first("content-type") == "application/grpc" &&
          grpc_payload_text(request.body) == "ntl-grpc-transform|request";
      if (!result.grpc_request_transformed)
        throw std::runtime_error(
            "managed HTTP/2 gRPC request was not transformed");
    }
    result.received_streams.push_back(transformed->stream_id);
  }
  result.multiplexed = result.received_streams ==
                       std::vector<std::uint32_t>{1, 5, 7, 11};

  for (const std::uint32_t stream_id : {11u, 7u, 5u, 1u}) {
    std::vector<std::byte> encoded;
    std::string coding;
    ntl::net::http::response_message response;
    response.wire_protocol = ntl::net::http::protocol::http2;
    response.status = 200;
    if (stream_id == 11) {
      encoded = grpc_wire("ntl-grpc-transform|request");
      response.headers.append("content-type", "application/grpc");
    } else {
      coding = stream_id == 7 ? "br" : stream_id == 5 ? "deflate" : "gzip";
      const std::string plain =
          "<html><body>controlled HTTP/2 origin stream " +
          std::to_string(stream_id) + "</body></html>";
      encoded = encode_content(
          encoders, std::as_bytes(std::span(plain)), coding);
      response.headers.append("content-type", "text/html; charset=utf-8");
      response.headers.append("content-encoding", coding);
    }
    response.headers.append("content-length",
                            std::to_string(encoded.size()));
    auto frames = ntl::net::http2::encode_response_frames(
        stream_id, response, encoded);
    if (!frames)
      throw std::runtime_error("managed HTTP/2 response encoding failed");
    co_await write_h2_frames(stream, *frames);
    result.response_streams.push_back(stream_id);
  }

  constexpr std::string_view extension =
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover";
  auto parameters =
      ntl::net::websocket::parse_permessage_deflate_response(extension);
  if (!parameters)
    throw std::runtime_error("managed HTTP/2 WebSocket extension is invalid");
  h2_wire_frame client_data;
  try {
    client_data = co_await read_h2_data_frame(stream, 9);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("origin WebSocket data: ") +
                             error.what());
  }
  auto client_frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(client_data.bytes));
  if (!client_frame ||
      client_frame->header().type != ntl::net::http2::frame_type::data ||
      client_frame->header().stream_id != 9)
    throw std::runtime_error("managed HTTP/2 WebSocket DATA is missing");
  const auto client_websocket = h2_data_payload(*client_frame);
  result.websocket_transformed =
      decode_websocket_text(client_websocket,
                            ntl::net::websocket::sender_role::client,
                            *parameters) ==
      "h2-client-message [ntl-kernel]";
  const auto server_websocket = encode_websocket_text(
      "h2-server-message", ntl::net::websocket::sender_role::server,
      *parameters);
  std::vector<ntl::net::http2::outbound_frame> server_data;
  if (!ntl::net::http2::transform_detail::append_data_frames(
           server_data, 9, server_websocket, false,
           ntl::net::http2::default_maximum_frame_size)
           .is_ok())
    throw std::runtime_error("managed HTTP/2 WebSocket response failed");
  co_await write_h2_frames(stream, server_data);

  h2_wire_frame client_close;
  try {
    client_close = co_await read_h2_data_frame(stream, 9);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("origin WebSocket close: ") +
                             error.what());
  }
  auto close_frame = ntl::net::http2::frame_view::parse(
      ntl::net::scatter_view::from_contiguous(client_close.bytes));
  if (!close_frame ||
      close_frame->header().type != ntl::net::http2::frame_type::data ||
      close_frame->header().stream_id != 9 ||
      !is_websocket_close(h2_data_payload(*close_frame),
                          ntl::net::websocket::sender_role::client))
    throw std::runtime_error("managed HTTP/2 WebSocket close is missing");
  const auto server_close =
      encode_websocket_close(ntl::net::websocket::sender_role::server);
  std::vector<ntl::net::http2::outbound_frame> close_frames;
  if (!ntl::net::http2::transform_detail::append_data_frames(
           close_frames, 9, server_close, true,
           ntl::net::http2::default_maximum_frame_size)
           .is_ok())
    throw std::runtime_error("managed HTTP/2 WebSocket close failed");
  co_await write_h2_frames(stream, close_frames);

  const auto goaway = make_h2_goaway(11);
  if (co_await stream.write_all(goaway) != goaway.size())
    throw std::runtime_error("managed HTTP/2 GOAWAY was short");
  result.goaway = true;
  try {
    co_await close_tls(stream);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("origin TLS close: ") + error.what());
  }
  co_return result;
}

std::future<http2_origin_result> start_http2_origin(
    const listener &origin, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate) {
  return std::async(std::launch::async,
                    [&origin, origin_certificate, client_certificate]() {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(origin.socket.get(), &readable);
    timeval timeout{15, 0};
    if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
      throw std::runtime_error("managed HTTP/2 origin accept timed out");
    auto native = accept_one(origin);
    const SOCKET accepted = native.get();
    std::atomic<bool> completed{false};
    std::jthread watchdog([&](std::stop_token stop) {
      // This single bounded session exercises multiplexing, compressed
      // bodies, gRPC, Extended CONNECT, WebSocket and GOAWAY. Driver Verifier
      // can legitimately push it beyond the former 20-second fixture limit.
      for (unsigned count = 0; count != 1200 && !stop.stop_requested();
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
      auto operation = serve_http2_origin(stream, client_certificate);
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

struct http2_client_result {
  std::array<unsigned, 5> statuses{};
  std::array<std::string, 5> codings{};
  std::array<std::string, 5> bodies{};
  std::vector<std::uint32_t> completion_order;
  bool settings = false;
  bool flow_control = false;
  bool extended_connect = false;
  bool websocket_transformed = false;
  bool websocket_closed = false;
  bool grpc_response_transformed = false;
  bool unsupported_connect_blocked = false;
  bool goaway = false;
};

std::optional<std::size_t> ordinary_h2_slot(std::uint32_t stream_id) {
  switch (stream_id) {
  case 1:
    return 0;
  case 3:
    return 1;
  case 5:
    return 2;
  case 7:
    return 3;
  case 11:
    return 4;
  default:
    return std::nullopt;
  }
}

ntl::net::user::task<http2_client_result> run_http2_client(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(managed_server_name),
       .application_protocols = {"h2"},
       .require_application_protocol = true});
  if (co_await stream.write_all(std::as_bytes(std::span(h2_preface))) !=
      h2_preface.size())
    throw std::runtime_error("managed HTTP/2 preface write was short");
  const auto settings = make_h2_settings(true, 32);
  if (co_await stream.write_all(settings) != settings.size())
    throw std::runtime_error("managed HTTP/2 client SETTINGS was short");

  http2_client_result result{};
  for (unsigned count = 0; count != 16 && !result.settings; ++count) {
    auto wire = co_await read_h2_frame(stream);
    if (wire.header.type != ntl::net::http2::frame_type::settings)
      throw std::runtime_error(
          "managed HTTP/2 origin did not start with SETTINGS");
    if (!wire.header.acknowledgement()) {
      result.settings = true;
      const auto acknowledgement = make_h2_settings_ack();
      if (co_await stream.write_all(acknowledgement) !=
          acknowledgement.size())
        throw std::runtime_error("managed HTTP/2 client ack was short");
    }
  }
  if (!result.settings)
    throw std::runtime_error("managed HTTP/2 origin SETTINGS timed out");

  std::array<std::pair<std::uint32_t, ntl::net::http::request_message>, 4>
      ordinary{{{1, make_h2_request("/gzip")},
                {3, make_h2_request("/blocked", true)},
                {5, make_h2_request("/deflate")},
                {7, make_h2_request("/br")}}};
  const auto websocket_request = make_h2_websocket_request();
  const auto grpc_body = grpc_wire(
      crtsys::wfp_browser_http_policy::grpc_transform_fixture);
  const auto grpc_request = make_h2_grpc_request(grpc_body);
  const auto unsupported_connect = make_h2_unsupported_connect_request();
  for (const auto &[stream_id, request] : ordinary) {
    auto frames = ntl::net::http2::encode_request_frames(stream_id, request,
                                                         {});
    if (!frames)
      throw std::runtime_error("managed HTTP/2 request encoding failed");
    co_await write_h2_frames(stream, *frames);
  }
  auto opening = ntl::net::http2::encode_request_frames(
      9, websocket_request, {}, ntl::net::http2::default_maximum_frame_size,
      256 * 1024, false);
  if (!opening)
    throw std::runtime_error(
        "managed HTTP/2 extended request encoding failed");
  co_await write_h2_frames(stream, *opening);
  auto grpc_frames = ntl::net::http2::encode_request_frames(
      11, grpc_request, grpc_body);
  if (!grpc_frames)
    throw std::runtime_error("managed HTTP/2 gRPC request encoding failed");
  co_await write_h2_frames(stream, *grpc_frames);
  auto unsupported_frames = ntl::net::http2::encode_request_frames(
      13, unsupported_connect, {},
      ntl::net::http2::default_maximum_frame_size, 256 * 1024, false);
  if (!unsupported_frames)
    throw std::runtime_error(
        "managed HTTP/2 unsupported CONNECT encoding failed");
  co_await write_h2_frames(stream, *unsupported_frames);

  ntl::net::http::transform_pipeline pipeline;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  exchanges->peer_extended_connect_enabled(true);
  for (const auto &[stream_id, request] : ordinary) {
    if (!exchanges->remember(stream_id, request).is_ok())
      throw std::runtime_error("managed HTTP/2 client state failed");
  }
  if (!exchanges->remember(9, websocket_request).is_ok())
    throw std::runtime_error("managed HTTP/2 tunnel state failed");
  if (!exchanges->admit_connect(
           9, ntl::net::http2::connect_disposition::inspect)
           .is_ok())
    throw std::runtime_error("managed HTTP/2 tunnel admission failed");
  if (!exchanges->remember(11, grpc_request).is_ok() ||
      !exchanges->remember(13, unsupported_connect).is_ok())
    throw std::runtime_error("managed HTTP/2 policy state failed");
  ntl::net::http2::connection_transformer responses(
      ntl::net::http2::connection_direction::responses, exchanges, pipeline,
      decoders, encoders);

  constexpr std::string_view extension =
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover";
  auto parameters =
      ntl::net::websocket::parse_permessage_deflate_response(extension);
  if (!parameters)
    throw std::runtime_error("managed HTTP/2 WebSocket extension is invalid");
  bool websocket_sent = false;
  bool websocket_close_sent = false;
  for (unsigned count = 0; count != 512; ++count) {
    auto wire = co_await read_h2_frame(stream);
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(wire.bytes),
        {1024 * 1024, false});
    if (!frame)
      throw std::runtime_error("managed HTTP/2 client frame is invalid");
    if (frame->header().type == ntl::net::http2::frame_type::settings) {
      if (!frame->header().acknowledgement()) {
        const auto acknowledgement = make_h2_settings_ack();
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("managed HTTP/2 SETTINGS ack was short");
      }
      continue;
    }
    if (frame->header().type == ntl::net::http2::frame_type::window_update)
      continue;
    if (frame->header().type == ntl::net::http2::frame_type::goaway) {
      result.goaway = true;
      if (result.completion_order.size() == 5 &&
          result.websocket_closed && result.unsupported_connect_blocked)
        break;
      continue;
    }
    if (frame->header().type == ntl::net::http2::frame_type::headers) {
      auto update = ntl::net::http2::encode_window_update(
          frame->header().stream_id, 65535);
      if (!update ||
          co_await stream.write_all(update->wire) != update->wire.size())
        throw std::runtime_error("managed HTTP/2 WINDOW_UPDATE was short");
      result.flow_control = true;
    }
    if (frame->header().type == ntl::net::http2::frame_type::data &&
        frame->header().stream_id == 9) {
      const auto payload = h2_data_payload(*frame);
      if (!websocket_close_sent) {
        result.websocket_transformed =
            decode_websocket_text(
                payload, ntl::net::websocket::sender_role::server,
                *parameters) == "h2-server-message [ntl-kernel]";
        const auto close = encode_websocket_close(
            ntl::net::websocket::sender_role::client);
        std::vector<ntl::net::http2::outbound_frame> close_frames;
        if (!ntl::net::http2::transform_detail::append_data_frames(
                 close_frames, 9, close, true,
                 ntl::net::http2::default_maximum_frame_size)
                 .is_ok())
          throw std::runtime_error(
              "managed HTTP/2 client close encoding failed");
        co_await write_h2_frames(stream, close_frames);
        websocket_close_sent = true;
      } else {
        result.websocket_closed = is_websocket_close(
            payload, ntl::net::websocket::sender_role::server);
      }
      continue;
    }

    auto transformed = responses.consume(*frame);
    if (!transformed)
      throw std::runtime_error("managed HTTP/2 response parse failed");
    if (!transformed->message_complete || !transformed->response)
      continue;
    if (transformed->stream_id == 9) {
      if (transformed->response->status != 200)
        throw std::runtime_error("managed HTTP/2 CONNECT was rejected");
      result.extended_connect = true;
      const auto websocket = encode_websocket_text(
          "h2-client-message", ntl::net::websocket::sender_role::client,
          *parameters);
      std::vector<ntl::net::http2::outbound_frame> data;
      if (!ntl::net::http2::transform_detail::append_data_frames(
               data, 9, websocket, false,
               ntl::net::http2::default_maximum_frame_size)
               .is_ok())
        throw std::runtime_error(
            "managed HTTP/2 client WebSocket encoding failed");
      co_await write_h2_frames(stream, data);
      websocket_sent = true;
      continue;
    }
    if (transformed->stream_id == 13) {
      result.unsupported_connect_blocked =
          transformed->response->status == 403;
      if (!result.unsupported_connect_blocked)
        throw std::runtime_error(
            "managed HTTP/2 unsupported CONNECT did not fail closed");
      continue;
    }
    const auto slot = ordinary_h2_slot(transformed->stream_id);
    if (!slot)
      throw std::runtime_error("managed HTTP/2 response stream is unknown");
    result.statuses[*slot] = transformed->response->status;
    if (const auto coding =
            transformed->response->headers.first("content-encoding"))
      result.codings[*slot] = std::string(*coding);
    if (transformed->stream_id == 11) {
      result.bodies[*slot] = grpc_payload_text(transformed->response->body);
      result.grpc_response_transformed =
          result.bodies[*slot] ==
          "ntl-grpc-transform|request|response";
    } else {
      result.bodies[*slot] = text_of(transformed->response->body);
    }
    result.completion_order.push_back(transformed->stream_id);
    if (result.goaway && result.completion_order.size() == 5 &&
        result.websocket_closed && result.unsupported_connect_blocked)
      break;
  }
  if (!websocket_sent)
    throw std::runtime_error("managed HTTP/2 WebSocket was not opened");
  co_await close_tls(stream);
  co_return result;
}

http2_client_result exchange_http2(int family, std::uint16_t port) {
  auto native = connect_loopback(family, port);
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  auto credentials = ntl::net::tls_credentials::client();
  ntl::net::tls_stream stream(socket, credentials);
  auto operation = run_http2_client(stream);
  auto result = ntl::net::user::sync_wait(std::move(operation));
  context.wait_for_idle();
  return result;
}


managed_tcp_acceptance_result require_http2(
    acceptance_controller &device,
    const contract::service_info &service, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate, capture_log &logger,
    std::uint64_t capture_baseline) {
  auto origin_v4 = make_listener();
  auto origin_v6 = make_ipv6_listener();
  auto server_v4 = start_http2_origin(origin_v4, origin_certificate,
                                      client_certificate);
  std::future<http2_origin_result> server_v6;
  http2_client_result client_v4;
  http2_client_result client_v6;
  const auto driver_diagnostics = [&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto service_snapshot = query_service(device);
    std::string result = "; driver active=" +
                         std::to_string(service_snapshot.active_tcp_sessions) +
                         " failed=" + std::to_string(service_snapshot.failed);
    std::uint64_t cursor = capture_baseline;
    for (;;) {
      const auto next = read_inspection(device, cursor);
      if (!next.available)
        break;
      cursor = next.record.sequence;
      result += " [seq=" + std::to_string(next.record.sequence) +
                " session=" + std::to_string(next.record.session_id) +
                " protocol=" +
                std::to_string(static_cast<unsigned>(next.record.protocol)) +
                " action=" +
                std::to_string(static_cast<unsigned>(next.record.action)) +
                " status=" + std::to_string(next.record.status) +
                " NTSTATUS=" +
                std::to_string(next.record.failure_status) + " flags=" +
                std::to_string(next.record.flags) + "]";
    }
    return result;
  };
  const auto origin_diagnostics = [](std::future<http2_origin_result> &server) {
    if (!server.valid())
      return std::string("; origin future unavailable");
    if (server.wait_for(2s) != std::future_status::ready)
      return std::string("; origin still pending");
    try {
      const auto result = server.get();
      return std::string("; origin completed streams=") +
             std::to_string(result.response_streams.size()) +
             " goaway=" + (result.goaway ? "1" : "0");
    } catch (const std::exception &error) {
      return std::string("; origin failed: ") + error.what();
    } catch (...) {
      return std::string("; origin failed: unknown exception");
    }
  };
  {
    auto policy = device.install_tcp_policy(origin_v4.port, origin_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("managed HTTP/2 WFP redirect is missing");
    try {
      client_v4 = exchange_http2(AF_INET, origin_v4.port);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("managed HTTP/2 IPv4 client: ") +
                               error.what() + driver_diagnostics() +
                               origin_diagnostics(server_v4));
    }
    // The IPv4 scenario deliberately exercises multiplexing, compression,
    // gRPC, extended CONNECT, GOAWAY, and TLS shutdown. Under Driver Verifier
    // it can exceed the origin's bounded accept timeout. Start the independent
    // IPv6 accept immediately before its client instead of consuming that
    // timeout while the IPv4 scenario is still running.
    server_v6 = start_http2_origin(origin_v6, origin_certificate,
                                   client_certificate);
    try {
      client_v6 = exchange_http2(AF_INET6, origin_v6.port);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("managed HTTP/2 IPv6 client: ") +
                               error.what() + driver_diagnostics() +
                               origin_diagnostics(server_v6));
    }
  }
  http2_origin_result origin_result_v4;
  http2_origin_result origin_result_v6;
  try {
    origin_result_v4 = server_v4.get();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("managed HTTP/2 IPv4 origin: ") +
                             error.what());
  }
  try {
    origin_result_v6 = server_v6.get();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("managed HTTP/2 IPv6 origin: ") +
                             error.what());
  }
  const auto valid_client = [](const http2_client_result &client) {
    return client.statuses ==
               std::array<unsigned, 5>{200, 403, 200, 200, 200} &&
           client.codings[0] == "gzip" && client.codings[1].empty() &&
           client.codings[2] == "deflate" && client.codings[3] == "br" &&
           client.codings[4].empty() && client.grpc_response_transformed &&
           client.bodies[0].find(kernel_transform_marker) !=
               std::string::npos &&
           client.bodies[2].find(kernel_transform_marker) !=
               std::string::npos &&
           client.bodies[3].find(kernel_transform_marker) !=
               std::string::npos &&
           client.bodies[1].find("blocked by browser inspection policy") !=
               std::string::npos &&
           client.settings && client.flow_control &&
           client.extended_connect && client.websocket_transformed &&
           client.websocket_closed && client.unsupported_connect_blocked &&
           client.goaway &&
           client.completion_order !=
               std::vector<std::uint32_t>{1, 3, 5, 7, 11};
  };
  const auto valid_origin = [](const http2_origin_result &origin) {
    return origin.mutual_tls && origin.transformed_headers &&
           origin.multiplexed && origin.extended_connect &&
           origin.websocket_transformed && origin.grpc_request_transformed &&
           origin.goaway &&
           origin.response_streams ==
               std::vector<std::uint32_t>{11, 7, 5, 1};
  };
  if (!valid_client(client_v4) || !valid_client(client_v6) ||
      !valid_origin(origin_result_v4) || !valid_origin(origin_result_v6))
    throw std::runtime_error(
        "managed HTTP/2 multiplex/flow/tunnel evidence is incomplete");

  const auto after = query_service(device);
  if (after.accepted < service.accepted + 2 ||
      after.handshaken < service.handshaken + 2 ||
      after.origin_connected < service.origin_connected + 2 ||
      after.origin_completed < service.origin_completed + 10 ||
      after.permitted < service.permitted + 10 ||
      after.blocked < service.blocked + 4 ||
      after.origin_peer_validated < service.origin_peer_validated + 2)
    throw std::runtime_error("managed HTTP/2 service counters are incomplete");

  std::uint64_t cursor = capture_baseline;
  unsigned permitted_v4 = 0;
  unsigned permitted_v6 = 0;
  unsigned blocked_v4 = 0;
  unsigned blocked_v6 = 0;
  unsigned compressed = 0;
  unsigned tunnels = 0;
  unsigned grpc = 0;
  std::uint64_t records = 0;
  for (;;) {
    const auto next = read_inspection(device, cursor);
    if (next.dropped != 0)
      throw std::runtime_error("managed HTTP/2 capture queue dropped records");
    if (!next.available)
      break;
    cursor = next.record.sequence;
    const auto &record = next.record;
    if (record.protocol != contract::inspected_protocol::http2)
      continue;
    logger.write(record);
    ++records;
    if (std::string_view(record.server_name.data(), record.server_name_size) !=
        managed_server_name_ascii)
      throw std::runtime_error(
          "managed HTTP/2 capture did not preserve SNI/tuple");
    unsigned *action_count = nullptr;
    if (record.original_family == AF_INET &&
        record.original_port == origin_v4.port) {
      action_count = record.action == contract::inspection_action::permitted
                         ? &permitted_v4
                         : &blocked_v4;
    } else if (record.original_family == AF_INET6 &&
               record.original_port == origin_v6.port) {
      action_count = record.action == contract::inspection_action::permitted
                         ? &permitted_v6
                         : &blocked_v6;
    } else {
      throw std::runtime_error(
          "managed HTTP/2 capture did not preserve the original tuple");
    }
    if (record.action != contract::inspection_action::permitted &&
        record.action != contract::inspection_action::blocked)
      throw std::runtime_error(
          "managed HTTP/2 capture has the wrong action: session=" +
          std::to_string(record.session_id) + " NTSTATUS=" +
          std::to_string(record.failure_status) + " flags=" +
          std::to_string(record.flags));
    ++*action_count;
    const auto required =
        record.action == contract::inspection_action::blocked
            ? contract::request_transformed
            : contract::request_transformed |
                  contract::response_transformed;
    if ((record.flags & required) != required)
      throw std::runtime_error("managed HTTP/2 capture missed transforms");
    if ((record.flags & contract::compressed_content) != 0)
      ++compressed;
    if ((record.flags & contract::websocket_or_extended_connect) != 0)
      ++tunnels;
    if ((record.flags & contract::grpc_message) != 0)
      ++grpc;
  }
  if (permitted_v4 != 5 || permitted_v6 != 5 || blocked_v4 != 2 ||
      blocked_v6 != 2 || compressed != 6 || tunnels != 4 || grpc != 2 ||
      records != 14)
    throw std::runtime_error("managed HTTP/2 capture evidence is incomplete");

  managed_tcp_acceptance_result result{};
  result.http2_policy_pipeline = true;
  result.http2_multiplexing = true;
  result.http2_flow_control = true;
  result.http2_goaway = true;
  result.http2_compression = true;
  result.http2_grpc = true;
  result.http2_extended_connect = true;
  result.http2_unsupported_connect_fail_closed = true;
  result.http2_websocket = true;
  result.http2_ipv4_ipv6_wfp = true;
  result.origin_system_validation = true;
  result.origin_exact_pin = true;
  result.origin_mtls = true;
  result.capture_records = records;
  return result;
}
