struct fallback_origin_result {
  bool mutual_tls = false;
  bool request_transformed = false;
  bool http2 = false;
  bool http1 = false;
};

struct fallback_origin_listeners {
  listener ipv4;
  listener ipv6;

  std::uint16_t port() const noexcept { return ipv4.port; }
};

fallback_origin_listeners make_fallback_origin_listeners() {
  // localhost is deliberately dual stack. Reserve a port on both loopback
  // families so the origin is the same application regardless of the address
  // order returned by WSK name resolution.
  for (unsigned attempt = 0; attempt != 64; ++attempt) {
    auto ipv4 = make_listener();
    try {
      auto ipv6 = make_ipv6_listener(ipv4.port);
      return {std::move(ipv4), std::move(ipv6)};
    } catch (const std::runtime_error &) {
      // A separately bound IPv6 endpoint may already own this ephemeral port.
      // Release the IPv4 socket and choose another port.
    }
  }
  throw std::runtime_error(
      "could not reserve one fallback origin port for IPv4 and IPv6");
}

ntl::net::user::task<fallback_origin_result> serve_fallback_http1_origin(
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
      {maximum_http_message_size}, 257);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error("fallback HTTP/1 origin ended early");
  const std::string wire = text_of(request->frame());
  fallback_origin_result result{};
  result.mutual_tls = true;
  result.http1 = true;
  result.request_transformed =
      request_path(wire) == "/allowed" &&
      ascii_contains_ci(wire, "x-ntl-inspected: 1\r\n");
  if (!result.request_transformed)
    throw std::runtime_error(
        "fallback HTTP/1 request was not transformed");
  constexpr std::string_view body =
      "<html><body>controlled fallback http/1.1 origin</body></html>";
  const std::string head =
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "Content-Length: " + std::to_string(body.size()) +
      "\r\nConnection: close\r\n\r\n";
  auto response = bytes_of(head);
  const auto body_bytes = std::as_bytes(std::span(body));
  response.insert(response.end(), body_bytes.begin(), body_bytes.end());
  if (co_await stream.write_all(response) != response.size())
    throw std::runtime_error("fallback HTTP/1 response was short");
  try {
    co_await close_tls(stream);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("fallback HTTP/1 TLS close: ") +
                             error.what());
  }
  co_return result;
}

ntl::net::user::task<fallback_origin_result> serve_fallback_http2_origin(
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
  co_await read_exactly(stream, preface);
  if (std::memcmp(preface.data(), h2_preface.data(), preface.size()) != 0)
    throw std::runtime_error("fallback HTTP/2 preface is invalid");
  const auto settings = make_h2_settings(false);
  if (co_await stream.write_all(settings) != settings.size())
    throw std::runtime_error("fallback HTTP/2 SETTINGS was short");

  ntl::net::http::transform_pipeline pipeline;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  ntl::net::inspection::register_standard_content_encoders(encoders);
  auto exchanges = std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer requests(
      ntl::net::http2::connection_direction::requests, exchanges, pipeline,
      decoders, encoders);
  fallback_origin_result result{};
  result.mutual_tls = true;
  result.http2 = true;
  for (unsigned count = 0; count != 128; ++count) {
    auto wire = co_await read_h2_frame(stream);
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(wire.bytes),
        {1024 * 1024, false});
    if (!frame)
      throw std::runtime_error("fallback HTTP/2 frame is invalid");
    if (frame->header().type == ntl::net::http2::frame_type::settings) {
      if (!frame->header().acknowledgement()) {
        const auto acknowledgement = make_h2_settings_ack();
        if (co_await stream.write_all(acknowledgement) !=
            acknowledgement.size())
          throw std::runtime_error("fallback HTTP/2 SETTINGS ack was short");
      }
      continue;
    }
    if (frame->header().type == ntl::net::http2::frame_type::window_update)
      continue;
    auto transformed = requests.consume(*frame);
    if (!transformed)
      throw std::runtime_error("fallback HTTP/2 request parse failed");
    if (!transformed->message_complete || !transformed->request)
      continue;
    const auto &request = *transformed->request;
    result.request_transformed =
        transformed->stream_id == 1 && request.method == "GET" &&
        request.path == "/allowed" &&
        request.headers.first("x-ntl-inspected") == "1";
    if (!result.request_transformed)
      throw std::runtime_error(
          "fallback HTTP/2 request was not transformed");
    constexpr std::string_view body =
        "<html><body>controlled fallback h2 origin</body></html>";
    ntl::net::http::response_message response;
    response.wire_protocol = ntl::net::http::protocol::http2;
    response.status = 200;
    response.headers.append("content-type", "text/html; charset=utf-8");
    response.headers.append("content-length", std::to_string(body.size()));
    const auto body_bytes = std::as_bytes(std::span(body));
    auto frames = ntl::net::http2::encode_response_frames(
        transformed->stream_id, response, body_bytes);
    if (!frames)
      throw std::runtime_error("fallback HTTP/2 response encoding failed");
    co_await write_h2_frames(stream, *frames);
    const auto goaway = make_h2_goaway(transformed->stream_id);
    if (co_await stream.write_all(goaway) != goaway.size())
      throw std::runtime_error("fallback HTTP/2 GOAWAY was short");
    try {
      co_await close_tls(stream);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("fallback HTTP/2 TLS close: ") +
                               error.what());
    }
    co_return result;
  }
  throw std::runtime_error("fallback HTTP/2 request timed out");
}

std::future<fallback_origin_result> start_fallback_origin(
    const fallback_origin_listeners &origin, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate, bool http2) {
  return std::async(
      std::launch::async,
      [&origin, origin_certificate, client_certificate, http2]() {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(origin.ipv4.socket.get(), &readable);
        FD_SET(origin.ipv6.socket.get(), &readable);
        // The strict H3 leg may exhaust its bounded dual-stack route budget
        // and drain both failed MsQuic connections before the bounded
        // TLS/TCP fallback begins. Keep the fixture alive for the complete
        // transition rather than racing the production timeouts.
        timeval timeout{45, 0};
        if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
          throw std::runtime_error("fallback origin accept timed out");
        const listener &selected =
            FD_ISSET(origin.ipv6.socket.get(), &readable) ? origin.ipv6
                                                          : origin.ipv4;
        auto native = accept_one(selected);
        const SOCKET accepted = native.get();
        std::atomic<bool> completed{false};
        std::jthread watchdog([&](std::stop_token stop) {
          for (unsigned count = 0; count != 400 && !stop.stop_requested();
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
        auto credentials =
            ntl::net::tls_credentials::server(origin_certificate);
        ntl::net::tls_stream stream(socket, credentials);
        try {
          auto operation = http2
                               ? serve_fallback_http2_origin(
                                     stream, client_certificate)
                               : serve_fallback_http1_origin(
                                     stream, client_certificate);
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

bool require_fallback_protocol(
    acceptance_controller &device,
    PCCERT_CONTEXT origin_certificate, PCCERT_CONTEXT client_certificate,
    bool http2) {
  auto origin = make_fallback_origin_listeners();
  auto server = start_fallback_origin(
      origin, origin_certificate, client_certificate, http2);
  const auto before = query_service(device);
  response client;
  {
    auto policy = device.install_http3_policy(origin.port());
    const auto &evidence = policy.http3_evidence();
    if (!complete_http3_policy_evidence(evidence))
      throw std::runtime_error("fallback WFP redirect is missing");
    const std::string authority =
        "localhost:" + std::to_string(origin.port());
    try {
      client = exchange_http3(
          AF_INET, origin.port(), "/allowed", false, true, authority);
    } catch (const std::exception &error) {
      const auto diagnostic = query_service(device);
      const auto &quic = diagnostic.quic_gate.ipv4;
      const auto &translation = diagnostic.quic_gate.translation;
      throw std::runtime_error(
          std::string(error.what()) + " service-ready=" +
          std::to_string(diagnostic.http3_ready) + " port=" +
          std::to_string(diagnostic.http3_port) + " accepted=" +
          std::to_string(diagnostic.http3_accepted) + " failed=" +
          std::to_string(diagnostic.http3_failed) + " active=" +
          std::to_string(diagnostic.http3_active_connections) +
          " classify=" + std::to_string(quic.classify_hits) + " blocked=" +
          std::to_string(quic.block_decisions) + " permit=" +
          std::to_string(quic.initial_permit) + " filter=" +
           std::to_string(quic.last_filter_id) + " remote-port=" +
           std::to_string(quic.last_remote_port) + " protocol=" +
           std::to_string(quic.last_protocol) + " udp-outbound=" +
           std::to_string(translation.outbound_packets) + " udp-inbound=" +
           std::to_string(translation.inbound_packets) + " udp-mappings=" +
           std::to_string(translation.mapping_updates) + " udp-misses=" +
           std::to_string(translation.mapping_misses) +
           " udp-map-family=" +
           std::to_string(translation.last_mapping_family) +
           " udp-map-source-port=" +
           std::to_string(translation.last_mapping_source_port) +
           " udp-map-destination-port=" +
           std::to_string(translation.last_mapping_destination_port) +
           " udp-map-proxy-port=" +
           std::to_string(translation.last_mapping_proxy_port) +
           " udp-resolve-peer-family=" +
           std::to_string(translation.last_resolution_peer_family) +
           " udp-resolve-peer-port=" +
           std::to_string(translation.last_resolution_peer_port) +
           " udp-resolve-proxy-family=" +
           std::to_string(translation.last_resolution_proxy_family) +
           " udp-resolve-proxy-port=" +
           std::to_string(translation.last_resolution_proxy_port) +
           " udp-resolve-status=" +
           std::to_string(translation.last_resolution_status) +
           " udp-injection-failures=" +
           std::to_string(translation.injection_failures) +
           " udp-quota-rejections=" +
           std::to_string(translation.quota_rejections) +
           " qpack-resumed=" + std::to_string(diagnostic.qpack_resumed) +
           " worker-requests=" +
           std::to_string(diagnostic.http3_worker_requests) +
           " pending-requests=" +
           std::to_string(diagnostic.http3_pending_requests) +
           " origin-connected=" +
           std::to_string(diagnostic.http3_origin_connected) +
           " origin-completed=" +
           std::to_string(diagnostic.http3_origin_completed) +
           " origin-failed=" +
           std::to_string(diagnostic.http3_origin_failed) +
           " origin-last-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.origin_last_status)) +
           " peer-bidi=" +
           std::to_string(diagnostic.http3_peer_bidirectional_started) +
           " peer-unidi=" +
           std::to_string(diagnostic.http3_peer_unidirectional_started) +
           " peer-receive=" +
           std::to_string(diagnostic.http3_peer_receive_events) +
           " peer-receive-fin=" +
           std::to_string(diagnostic.http3_peer_receive_fin_events) +
           " peer-send-shutdown=" +
           std::to_string(diagnostic.http3_peer_send_shutdown_events) +
           " request-classified=" +
           std::to_string(diagnostic.http3_request_streams_classified) +
           " request-sink-calls=" +
           std::to_string(diagnostic.http3_request_sink_calls) +
           " request-sink-final=" +
           std::to_string(diagnostic.http3_request_sink_final_calls) +
           " request-sink-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_request_sink_status)) +
           " stream-rejection-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_last_stream_rejection_status)) +
           " proxy-request-calls=" +
           std::to_string(diagnostic.http3_proxy_request_stream_calls) +
           " proxy-request-final=" +
           std::to_string(
               diagnostic.http3_proxy_request_stream_final_calls) +
           " proxy-retries=" +
           std::to_string(
               diagnostic.http3_proxy_request_inspector_retries) +
           " proxy-headers=" +
           std::to_string(diagnostic.http3_proxy_request_headers) +
           " proxy-ends=" +
           std::to_string(diagnostic.http3_proxy_request_stream_ends) +
           " proxy-blocked=" +
           std::to_string(
               diagnostic.http3_proxy_blocked_request_streams) +
           " proxy-active=" +
           std::to_string(diagnostic.http3_proxy_active_requests) +
           " proxy-submit-calls=" +
           std::to_string(diagnostic.http3_proxy_origin_submit_calls) +
           " proxy-end-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_proxy_last_stream_end_status)) +
           " proxy-submit-status=" +
           std::to_string(static_cast<std::uint32_t>(
               diagnostic.http3_proxy_last_origin_submit_status)));
    }
  }
  fallback_origin_result server_result{};
  try {
    server_result = server.get();
  } catch (const std::exception &error) {
    const auto diagnostic = query_service(device);
    throw std::runtime_error(
        std::string(error.what()) + " client-status=" +
        std::to_string(client.status) + " origin-failed=" +
        std::to_string(diagnostic.http3_origin_failed) +
        " fallback-attempted=" +
        std::to_string(diagnostic.origin_fallback_attempted) +
        " fallback-succeeded=" +
        std::to_string(diagnostic.origin_fallback_succeeded) +
        " fallback-rejected=" +
        std::to_string(diagnostic.origin_fallback_rejected) +
        " last-status=" + std::to_string(static_cast<std::uint32_t>(
            diagnostic.origin_last_status)) +
        " last-kind=" +
        std::to_string(diagnostic.origin_last_failure_kind) +
        " last-stage=" +
        std::to_string(static_cast<std::uint32_t>(
            diagnostic.origin_last_failure_stage)) +
        " fallback-phase=" +
        std::to_string(static_cast<std::uint32_t>(
            diagnostic.origin_last_fallback_phase)) +
        " origin-port=" + std::to_string(origin.port()) +
        " service-tcp-v4=" + std::to_string(diagnostic.tcp_port_v4) +
        " service-tcp-v6=" + std::to_string(diagnostic.tcp_port_v6));
  }
  const auto after = query_service(device);
  const std::string body = text_of(client.body);
  const std::string_view origin_marker =
      http2 ? std::string_view("controlled fallback h2 origin")
            : std::string_view("controlled fallback http/1.1 origin");
  if (client.status != 200 || !client.dynamic_qpack_acknowledged ||
      body.find(origin_marker) == std::string::npos ||
      body.find(kernel_transform_marker) == std::string::npos ||
      !server_result.mutual_tls || !server_result.request_transformed ||
      server_result.http2 != http2 || server_result.http1 == http2 ||
      after.origin_fallback_attempted !=
          before.origin_fallback_attempted + 1 ||
      after.origin_fallback_succeeded !=
          before.origin_fallback_succeeded + 1 ||
      after.origin_fallback_h2 !=
          before.origin_fallback_h2 + (http2 ? 1 : 0) ||
      after.origin_fallback_http1 !=
          before.origin_fallback_http1 + (http2 ? 0 : 1) ||
      after.origin_fallback_rejected != before.origin_fallback_rejected)
    throw std::runtime_error(
        std::string(http2 ? "managed H3 to H2 fallback evidence is incomplete"
                          : "managed H3 to HTTP/1 fallback evidence is incomplete") +
        " client-status=" + std::to_string(client.status) +
        " qpack-ack=" +
        std::to_string(client.dynamic_qpack_acknowledged) +
        " origin-marker=" +
        std::to_string(body.find(origin_marker) != std::string::npos) +
        " transform-marker=" +
        std::to_string(body.find(kernel_transform_marker) !=
                       std::string::npos) +
        " mtls=" + std::to_string(server_result.mutual_tls) +
        " request-transform=" +
        std::to_string(server_result.request_transformed) +
        " server-h2=" + std::to_string(server_result.http2) +
        " server-h1=" + std::to_string(server_result.http1) +
        " attempted-delta=" +
        std::to_string(after.origin_fallback_attempted -
                       before.origin_fallback_attempted) +
        " succeeded-delta=" +
        std::to_string(after.origin_fallback_succeeded -
                       before.origin_fallback_succeeded) +
        " h2-delta=" +
        std::to_string(after.origin_fallback_h2 - before.origin_fallback_h2) +
        " h1-delta=" +
        std::to_string(after.origin_fallback_http1 -
                       before.origin_fallback_http1) +
        " rejected-delta=" +
        std::to_string(after.origin_fallback_rejected -
                       before.origin_fallback_rejected));
  return true;
}

bool require_non_safe_fallback_rejection(
    acceptance_controller &device) {
  auto reachable_tcp_origin = make_listener();
  const auto before = query_service(device);
  response client;
  {
    auto policy = device.install_http3_policy(reachable_tcp_origin.port);
    const auto &evidence = policy.http3_evidence();
    if (!complete_http3_policy_evidence(evidence))
      throw std::runtime_error("non-safe fallback WFP redirect is missing");
    const std::string authority =
        "localhost:" + std::to_string(reachable_tcp_origin.port);
    client = exchange_http3_grpc(
        AF_INET, reachable_tcp_origin.port, authority);
  }
  const auto after = query_service(device);
  if (client.status != 502 ||
      text_of(client.body).find("validated origin transport unavailable") ==
          std::string::npos ||
      has_pending_connection(reachable_tcp_origin) ||
      after.origin_fallback_attempted != before.origin_fallback_attempted ||
      after.origin_fallback_succeeded != before.origin_fallback_succeeded ||
      after.origin_fallback_h2 != before.origin_fallback_h2 ||
      after.origin_fallback_http1 != before.origin_fallback_http1 ||
      after.origin_fallback_rejected !=
          before.origin_fallback_rejected + 1)
    throw std::runtime_error(
        "non-safe POST request was replayed through origin fallback");
  return true;
}
