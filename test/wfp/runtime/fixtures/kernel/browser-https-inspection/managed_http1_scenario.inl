struct http1_origin_result {
  unsigned requests = 0;
  bool transformed_headers = true;
  bool mutual_tls = false;
  bool persistent_connection = false;
  bool grpc_request_transformed = false;
};

ntl::net::user::task<http1_origin_result> serve_http1_origin(
    ntl::net::tls_stream &stream, PCCERT_CONTEXT client_certificate) {
  auto client_policy =
      std::make_shared<ntl::net::exact_client_certificate_policy>(
          client_certificate);
  co_await stream.handshake_server(
      {.application_protocols = {"http/1.1"},
       .require_application_protocol = true,
       .require_client_certificate = true,
       .client_certificate_policy = std::move(client_policy)});

  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_encoders(encoders);
  ntl::net::tls_framed_stream requests(
      stream, make_http_framer(ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 257);
  http1_origin_result result{};
  result.mutual_tls = true;
  for (unsigned index = 0; index != 4; ++index) {
    auto request = co_await requests.read_frame_or_eof();
    if (!request)
      throw std::runtime_error("managed HTTP/1 origin ended early");
    const std::string wire = text_of(request->frame());
    const std::string_view path = request_path(wire);
    if (path != "/gzip" && path != "/deflate" && path != "/br" &&
        path != "/grpc")
      throw std::runtime_error("blocked HTTP/1 request reached the origin");
    ++result.requests;
    result.transformed_headers =
        result.transformed_headers &&
        ascii_contains_ci(wire, "x-ntl-inspected: 1\r\n");

    std::vector<std::byte> encoded;
    std::string content_type;
    std::string coding;
    if (path == "/grpc") {
      const auto body = http1_body(request->frame());
      result.grpc_request_transformed =
          grpc_payload_text(body) == "ntl-grpc-transform|request";
      if (!result.grpc_request_transformed ||
          !ascii_contains_ci(wire, "content-type: application/grpc"))
        throw std::runtime_error(
            "managed HTTP/1 gRPC request was not transformed");
      encoded.assign(body.begin(), body.end());
      content_type = "application/grpc";
    } else {
      const std::string plain =
          "<html><body>controlled HTTP/1 origin " + std::string(path) +
          "</body></html>";
      coding = path == "/gzip" ? "gzip" : path == "/deflate" ? "deflate"
                                                                    : "br";
      encoded = encode_content(
          encoders, std::as_bytes(std::span(plain)), coding);
      content_type = "text/html; charset=utf-8";
    }
    std::string head = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type +
                       "\r\n";
    if (!coding.empty())
      head += "Content-Encoding: " + coding + "\r\n";
    head += "Content-Length: " + std::to_string(encoded.size()) + "\r\n";
    if (index + 1 == 4)
      head += "Connection: close\r\n";
    else
      head += "Connection: keep-alive\r\n";
    head += "\r\n";
    std::vector<std::byte> response = bytes_of(head);
    response.insert(response.end(), encoded.begin(), encoded.end());
    if (co_await stream.write_all(response) != response.size())
      throw std::runtime_error("managed HTTP/1 origin response was short");
  }
  result.persistent_connection = result.requests == 4;
  co_await close_tls(stream);
  co_return result;
}

std::future<http1_origin_result> start_http1_origin(
    const listener &origin, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate) {
  return std::async(std::launch::async,
                    [&origin, origin_certificate, client_certificate]() {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(origin.socket.get(), &readable);
    timeval timeout{10, 0};
    if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
      throw std::runtime_error("managed HTTP/1 origin accept timed out");
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
      auto operation = serve_http1_origin(stream, client_certificate);
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

struct http1_client_result {
  std::array<unsigned, 5> statuses{};
  std::array<std::string, 5> codings{};
  std::array<std::string, 5> bodies{};
  bool grpc_response_transformed = false;
};

std::string make_http1_request(std::string_view path, bool block,
                               bool close) {
  std::string request = "GET " + std::string(path) +
                        " HTTP/1.1\r\nHost: localhost\r\n";
  if (block)
    request += "X-NTL-Block: 1\r\n";
  request += close ? "Connection: close\r\n\r\n"
                   : "Connection: keep-alive\r\n\r\n";
  return request;
}

std::string make_http1_grpc_request() {
  const auto wire = grpc_wire(
      crtsys::wfp_browser_http_policy::grpc_transform_fixture);
  std::string request =
      "POST /grpc HTTP/1.1\r\nHost: localhost\r\n"
      "Content-Type: application/grpc\r\nContent-Length: " +
      std::to_string(wire.size()) +
      "\r\nConnection: close\r\n\r\n";
  request.append(reinterpret_cast<const char *>(wire.data()), wire.size());
  return request;
}

ntl::net::user::task<http1_client_result> run_http1_client(
    ntl::net::tls_stream &stream) {
  co_await stream.handshake_client(
      {.server_name = std::wstring(managed_server_name),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  const std::array<std::string, 5> requests{
      make_http1_request("/gzip", false, false),
      make_http1_request("/blocked", true, false),
      make_http1_request("/deflate", false, false),
      make_http1_request("/br", false, false),
      make_http1_grpc_request()};
  std::vector<std::byte> pipelined;
  for (const auto &request : requests) {
    const auto bytes = std::as_bytes(std::span(request));
    pipelined.insert(pipelined.end(), bytes.begin(), bytes.end());
  }
  if (co_await stream.write_all(pipelined) != pipelined.size())
    throw std::runtime_error("managed HTTP/1 pipelined write was short");

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::tls_framed_stream replies(
      stream, make_http_framer(ntl::net::http::http1_message_kind::response,
                               true),
      {maximum_http_message_size}, 233);
  http1_client_result result{};
  for (std::size_t index = 0; index != result.statuses.size(); ++index) {
    auto reply = co_await replies.read_frame_or_eof();
    if (!reply)
      throw std::runtime_error("managed HTTP/1 proxy returned too few replies");
    auto parsed = parse_http_response(*reply);
    result.statuses[index] = parsed.status;
    result.codings[index] = parsed.content_encoding;
    if (index + 1 == result.statuses.size()) {
      result.bodies[index] = grpc_payload_text(parsed.body);
      result.grpc_response_transformed =
          result.bodies[index] ==
          "ntl-grpc-transform|request|response";
    } else {
      const auto decoded = decode_content(decoders, parsed.body,
                                          parsed.content_encoding);
      result.bodies[index] = text_of(decoded);
    }
  }
  co_await close_tls(stream);
  co_return result;
}

http1_client_result exchange_http1(int family, std::uint16_t port) {
  auto native = connect_loopback(family, port);
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  auto credentials = ntl::net::tls_credentials::client();
  ntl::net::tls_stream stream(socket, credentials);
  auto operation = run_http1_client(stream);
  auto result = ntl::net::user::sync_wait(std::move(operation));
  context.wait_for_idle();
  return result;
}


managed_tcp_acceptance_result require_http1(
    acceptance_controller &device,
    const contract::service_info &service, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate, capture_log &logger,
    std::uint64_t capture_baseline) {
  auto origin_v4 = make_listener();
  auto origin_v6 = make_ipv6_listener();
  auto server_v4 = start_http1_origin(origin_v4, origin_certificate,
                                      client_certificate);
  std::future<http1_origin_result> server_v6;
  http1_client_result client_v4;
  http1_client_result client_v6;
  {
    auto policy = device.install_tcp_policy(origin_v4.port, origin_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("managed HTTP/1 WFP redirect is missing");
    client_v4 = exchange_http1(AF_INET, origin_v4.port);
    // Give each address family its own bounded accept window. The IPv6
    // listener remains available for policy installation, but its worker must
    // not time out while Driver Verifier slows the complete IPv4 scenario.
    server_v6 = start_http1_origin(origin_v6, origin_certificate,
                                   client_certificate);
    client_v6 = exchange_http1(AF_INET6, origin_v6.port);
  }
  const auto server_result_v4 = server_v4.get();
  const auto server_result_v6 = server_v6.get();
  const auto valid_client = [](const http1_client_result &client) {
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
               std::string::npos;
  };
  const auto valid_server = [](const http1_origin_result &server) {
    return server.requests == 4 && server.transformed_headers &&
           server.grpc_request_transformed && server.mutual_tls &&
           server.persistent_connection;
  };
  const auto describe_client = [](const http1_client_result &client) {
    std::string text = "statuses=";
    for (const auto value : client.statuses)
      text += std::to_string(value) + ",";
    text += " codings=";
    for (const auto &value : client.codings)
      text += "[" + value + "]";
    text += " grpc=" + std::to_string(client.grpc_response_transformed) +
            " markers=" +
            std::to_string(client.bodies[0].find(kernel_transform_marker) !=
                           std::string::npos) +
            std::to_string(client.bodies[2].find(kernel_transform_marker) !=
                           std::string::npos) +
            std::to_string(client.bodies[3].find(kernel_transform_marker) !=
                           std::string::npos) +
            " blocked=" +
            std::to_string(client.bodies[1].find(
                               "blocked by browser inspection policy") !=
                           std::string::npos);
    return text;
  };
  const auto describe_server = [](const http1_origin_result &server) {
    return "requests=" + std::to_string(server.requests) +
           " headers=" + std::to_string(server.transformed_headers) +
           " grpc=" + std::to_string(server.grpc_request_transformed) +
           " mtls=" + std::to_string(server.mutual_tls) +
           " persistent=" + std::to_string(server.persistent_connection);
  };
  if (!valid_client(client_v4) || !valid_client(client_v6) ||
      !valid_server(server_result_v4) || !valid_server(server_result_v6))
    throw std::runtime_error(
        "managed HTTP/1 WFP/pipeline/codec evidence is incomplete: v4-client{" +
        describe_client(client_v4) + "} v6-client{" +
        describe_client(client_v6) + "} v4-origin{" +
        describe_server(server_result_v4) + "} v6-origin{" +
        describe_server(server_result_v6) + "}");

  auto websocket_origin = make_listener();
  auto websocket_unused_v6 = make_ipv6_listener();
  auto websocket_server = start_websocket_origin(
      websocket_origin, origin_certificate, client_certificate);
  websocket_exchange_result websocket_client;
  {
    auto policy = device.install_tcp_policy(websocket_origin.port,
                                            websocket_unused_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("managed WebSocket WFP redirect is missing");
    websocket_client = exchange_websocket(AF_INET, websocket_origin.port);
  }
  const auto websocket_server_result = websocket_server.get();
  if (!websocket_client.server_message_transformed ||
      !websocket_client.compressed || !websocket_client.closed ||
      !websocket_server_result.mutual_tls ||
      !websocket_server_result.request_transformed ||
      !websocket_server_result.client_message_transformed ||
      !websocket_server_result.compressed || !websocket_server_result.closed ||
      has_pending_connection(websocket_unused_v6))
    throw std::runtime_error(
        "managed WebSocket permessage-deflate evidence is incomplete");
  const auto after = query_service(device);
  if (after.accepted < service.accepted + 3 ||
      after.handshaken < service.handshaken + 3 ||
      after.origin_connected < service.origin_connected + 3 ||
      after.origin_completed < service.origin_completed + 9 ||
      after.permitted < service.permitted + 9 ||
      after.blocked < service.blocked + 2 ||
      after.origin_peer_validated < service.origin_peer_validated + 3)
    throw std::runtime_error("managed HTTP/1 service counters are incomplete");

  std::uint64_t cursor = capture_baseline;
  unsigned permitted_v4 = 0;
  unsigned permitted_v6 = 0;
  unsigned blocked_v4 = 0;
  unsigned blocked_v6 = 0;
  unsigned websocket_records = 0;
  unsigned grpc_records = 0;
  std::uint64_t records = 0;
  for (;;) {
    const auto next = read_inspection(device, cursor);
    if (next.dropped != 0)
      throw std::runtime_error("managed HTTP/1 capture queue dropped records");
    if (!next.available)
      break;
    cursor = next.record.sequence;
    const auto &record = next.record;
    if (record.protocol != contract::inspected_protocol::http1)
      continue;
    logger.write(record);
    ++records;
    const std::string_view captured_server_name(
        record.server_name.data(), record.server_name_size);
    if (captured_server_name != managed_server_name_ascii)
      throw std::runtime_error(
          "managed HTTP/1 capture has the wrong SNI: sequence=" +
          std::to_string(record.sequence) + " expected=" +
          std::string(managed_server_name_ascii) + " actual=[" +
          std::string(captured_server_name) + "] family=" +
          std::to_string(record.original_family) + " port=" +
          std::to_string(record.original_port));
    unsigned *count = nullptr;
    const bool websocket =
        (record.flags & contract::websocket_or_extended_connect) != 0;
    if (websocket && record.original_family == AF_INET &&
        record.original_port == websocket_origin.port &&
        record.action == contract::inspection_action::permitted) {
      ++websocket_records;
      count = nullptr;
    } else if (record.original_family == AF_INET &&
        record.original_port == origin_v4.port) {
      count = record.action == contract::inspection_action::permitted
                  ? &permitted_v4
                  : &blocked_v4;
    } else if (record.original_family == AF_INET6 &&
               record.original_port == origin_v6.port) {
      count = record.action == contract::inspection_action::permitted
                  ? &permitted_v6
                  : &blocked_v6;
    } else {
      throw std::runtime_error(
          "managed HTTP/1 capture did not preserve the original tuple");
    }
    if (record.action != contract::inspection_action::permitted &&
        record.action != contract::inspection_action::blocked)
      throw std::runtime_error("managed HTTP/1 capture has the wrong action");
    if (count)
      ++*count;
    const bool grpc = (record.flags & contract::grpc_message) != 0;
    if (grpc)
      ++grpc_records;
    const auto required = contract::request_transformed |
                          contract::response_transformed;
    if ((record.flags & required) != required ||
        (record.action == contract::inspection_action::permitted &&
         !websocket && !grpc &&
         (record.flags & contract::compressed_content) == 0))
      throw std::runtime_error("managed HTTP/1 capture flags are incomplete");
  }
  if (permitted_v4 != 4 || permitted_v6 != 4 || blocked_v4 != 1 ||
      blocked_v6 != 1 || websocket_records != 1 || grpc_records != 2 ||
      records != 11)
    throw std::runtime_error("managed HTTP/1 capture evidence is incomplete");
  managed_tcp_acceptance_result result{};
  result.http1_policy_pipeline = true;
  result.http1_pipelining = true;
  result.http1_compression = true;
  result.http1_grpc = true;
  result.http1_websocket = true;
  result.http1_ipv4_ipv6_wfp = true;
  result.origin_system_validation = true;
  result.origin_exact_pin = true;
  result.origin_mtls = true;
  result.capture_records = records;
  return result;
}
