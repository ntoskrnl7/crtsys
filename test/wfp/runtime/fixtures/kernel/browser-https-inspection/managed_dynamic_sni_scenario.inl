struct sni_exchange_result {
  bool mutual_tls = false;
  bool transformed_request = false;
  bool transformed_response = false;
  std::vector<std::byte> peer_leaf;
};

class recording_peer_certificate_policy final
    : public ntl::net::tls_peer_certificate_policy {
public:
  bool verify(PCCERT_CONTEXT certificate,
              std::wstring_view) noexcept override {
    if (!certificate || !certificate->pbCertEncoded ||
        certificate->cbCertEncoded == 0)
      return false;
    try {
      leaf_.assign(
          reinterpret_cast<const std::byte *>(certificate->pbCertEncoded),
          reinterpret_cast<const std::byte *>(certificate->pbCertEncoded +
                                               certificate->cbCertEncoded));
      return true;
    } catch (...) {
      leaf_.clear();
      return false;
    }
  }

  const std::vector<std::byte> &leaf() const noexcept { return leaf_; }

private:
  std::vector<std::byte> leaf_;
};

ntl::net::user::task<sni_exchange_result> serve_sni_origin(
    ntl::net::tls_stream &stream, PCCERT_CONTEXT client_certificate,
    std::string server_name) {
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
      {maximum_http_message_size}, 239);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error("dynamic SNI origin received no request");
  const std::string wire = text_of(request->frame());
  const bool transformed =
      ascii_contains_ci(wire, "x-ntl-inspected: 1\r\n") &&
      ascii_contains_ci(wire, "host: " + server_name + "\r\n");
  const std::string body =
      "<html><body>dynamic SNI origin " + server_name + "</body></html>";
  const std::string head =
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "Content-Length: " +
      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  std::vector<std::byte> response = bytes_of(head);
  const auto body_bytes = std::as_bytes(std::span(body));
  response.insert(response.end(), body_bytes.begin(), body_bytes.end());
  if (co_await stream.write_all(response) != response.size())
    throw std::runtime_error("dynamic SNI origin response was short");
  co_await close_tls(stream);
  co_return sni_exchange_result{true, transformed, false, {}};
}

std::future<sni_exchange_result> start_sni_origin(
    const listener &origin, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate, std::string server_name) {
  return std::async(
      std::launch::async,
      [&origin, origin_certificate, client_certificate,
       server_name = std::move(server_name)]() {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(origin.socket.get(), &readable);
        timeval timeout{15, 0};
        if (::select(0, &readable, nullptr, nullptr, &timeout) != 1)
          throw std::runtime_error("dynamic SNI origin accept timed out");
        auto native = accept_one(origin);
        const SOCKET accepted = native.get();
        std::atomic<bool> completed{false};
        std::jthread watchdog([&](std::stop_token stop) {
          for (unsigned count = 0; count != 300 && !stop.stop_requested();
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
          auto operation = serve_sni_origin(
              stream, client_certificate, server_name);
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

ntl::net::user::task<unsigned> handshake_sni_client(
    ntl::net::tls_stream &stream, std::string server_name,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> certificate_policy =
        {}) {
  const std::wstring wide(server_name.begin(), server_name.end());
  co_await stream.handshake_client(
      {.server_name = wide,
       .certificate_policy = std::move(certificate_policy),
       .application_protocols = {"http/1.1"},
       .require_application_protocol = true});
  co_return 0;
}

ntl::net::user::task<sni_exchange_result> request_sni_client(
    ntl::net::tls_stream &stream, std::string server_name) {
  const std::string request =
      "GET /identity HTTP/1.1\r\nHost: " + server_name +
      "\r\nConnection: close\r\n\r\n";
  if (co_await stream.write_all(std::as_bytes(std::span(request))) !=
      request.size())
    throw std::runtime_error("dynamic SNI client request was short");
  ntl::net::tls_framed_stream replies(
      stream, make_http_framer(ntl::net::http::http1_message_kind::response,
                               true),
      {maximum_http_message_size}, 241);
  auto reply = co_await replies.read_frame_or_eof();
  if (!reply)
    throw std::runtime_error("dynamic SNI proxy returned no response");
  auto parsed = parse_http_response(*reply);
  const std::string body = text_of(parsed.body);
  co_await close_tls(stream);
  co_return sni_exchange_result{
      false, false,
      parsed.status == 200 &&
          body.find("dynamic SNI origin " + server_name) !=
              std::string::npos &&
          body.find(kernel_transform_marker) != std::string::npos,
      {}};
}

sni_exchange_result exchange_sni(int family, std::uint16_t port,
                                 std::string server_name) {
  auto native = connect_loopback(family, port);
  ntl::net::io_completion_context context;
  ntl::net::async_socket socket(context, native.release());
  auto credentials = ntl::net::tls_credentials::client();
  ntl::net::tls_stream stream(socket, credentials);
  auto certificate = std::make_shared<recording_peer_certificate_policy>();
  auto handshake = handshake_sni_client(stream, server_name, certificate);
  (void)ntl::net::user::sync_wait(std::move(handshake));
  auto request = request_sni_client(stream, std::move(server_name));
  auto result = ntl::net::user::sync_wait(std::move(request));
  result.peer_leaf = certificate->leaf();
  context.wait_for_idle();
  return result;
}

bool rejected_sni(int family, std::uint16_t port, std::string server_name) {
  try {
    auto native = connect_loopback(family, port);
    ntl::net::io_completion_context context;
    {
      ntl::net::async_socket socket(context, native.release());
      auto credentials = ntl::net::tls_credentials::client();
      ntl::net::tls_stream stream(socket, credentials);
      auto handshake = handshake_sni_client(stream, std::move(server_name));
      (void)ntl::net::user::sync_wait(std::move(handshake));
    }
    context.wait_for_idle();
    return false;
  } catch (...) {
    return true;
  }
}

dynamic_sni_acceptance_result require_dynamic_sni(
    acceptance_controller &device,
    const contract::service_info &service, std::string server_name,
    PCCERT_CONTEXT origin_certificate, PCCERT_CONTEXT client_certificate,
    const std::function<void()> &replace_identity) {
  auto lifetime_origin = make_listener();
  auto unused_v6 = make_ipv6_listener();
  auto lifetime_server = start_sni_origin(
      lifetime_origin, origin_certificate, client_certificate, server_name);
  std::promise<void> handshaken;
  auto handshaken_future = handshaken.get_future();
  std::promise<std::vector<std::byte>> presented_leaf;
  auto presented_leaf_future = presented_leaf.get_future();
  std::promise<void> continue_request;
  auto continue_future = continue_request.get_future().share();
  std::future<sni_exchange_result> lifetime_client;
  std::vector<std::byte> pre_replacement_leaf;
  const auto failure_cursor =
      read_inspection(device, (std::numeric_limits<std::uint64_t>::max)())
          .current_sequence;
  {
    auto policy = device.install_tcp_policy(lifetime_origin.port,
                                            unused_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("dynamic SNI WFP redirect is missing");
    lifetime_client = std::async(
        std::launch::async,
        [port = lifetime_origin.port, server_name, &handshaken,
         &presented_leaf,
         continue_future]() mutable {
          auto native = connect_loopback(AF_INET, port);
          ntl::net::io_completion_context context;
          ntl::net::async_socket socket(context, native.release());
          auto credentials = ntl::net::tls_credentials::client();
          ntl::net::tls_stream stream(socket, credentials);
          auto certificate =
              std::make_shared<recording_peer_certificate_policy>();
          auto handshake =
              handshake_sni_client(stream, server_name, certificate);
          (void)ntl::net::user::sync_wait(std::move(handshake));
          presented_leaf.set_value(certificate->leaf());
          handshaken.set_value();
          continue_future.wait();
          auto request = request_sni_client(stream, server_name);
          auto result = ntl::net::user::sync_wait(std::move(request));
          result.peer_leaf = certificate->leaf();
          context.wait_for_idle();
          return result;
        });
    if (handshaken_future.wait_for(10s) != std::future_status::ready) {
      if (lifetime_client.wait_for(0s) == std::future_status::ready) {
        try {
          (void)lifetime_client.get();
        } catch (const std::exception &error) {
          auto failure = read_inspection(device, failure_cursor);
          for (unsigned attempt = 0; !failure.available && attempt != 50;
               ++attempt) {
            std::this_thread::sleep_for(20ms);
            failure = read_inspection(device, failure_cursor);
          }
          if (failure.available)
            throw std::runtime_error(
                std::string(error.what()) + "; driver NTSTATUS=" +
                std::to_string(failure.record.failure_status));
          const auto service_snapshot = query_service(device);
          throw std::runtime_error(
              std::string(error.what()) + "; accepted=" +
              std::to_string(service_snapshot.accepted) + "; handshaken=" +
              std::to_string(service_snapshot.handshaken) + "; failed=" +
              std::to_string(service_snapshot.failed) + "; active=" +
              std::to_string(service_snapshot.active_tcp_sessions) +
              "; inspection-sequence=" +
              std::to_string(failure.current_sequence));
        }
      }
      throw std::runtime_error("dynamic SNI handshake timed out");
    }
    pre_replacement_leaf = presented_leaf_future.get();
    if (pre_replacement_leaf.empty())
      throw std::runtime_error("dynamic SNI peer leaf was not observed");
    replace_identity();
    continue_request.set_value();
    sni_exchange_result client;
    try {
      client = lifetime_client.get();
    } catch (const std::exception &error) {
      auto failure = read_inspection(device, failure_cursor);
      for (unsigned attempt = 0; !failure.available && attempt != 50;
           ++attempt) {
        std::this_thread::sleep_for(20ms);
        failure = read_inspection(device, failure_cursor);
      }
      if (failure.available)
        throw std::runtime_error(
            std::string(error.what()) + "; driver NTSTATUS=" +
            std::to_string(failure.record.failure_status));
      const auto service_snapshot = query_service(device);
      throw std::runtime_error(
          std::string(error.what()) + "; accepted=" +
          std::to_string(service_snapshot.accepted) + "; handshaken=" +
          std::to_string(service_snapshot.handshaken) + "; failed=" +
          std::to_string(service_snapshot.failed) + "; active=" +
          std::to_string(service_snapshot.active_tcp_sessions) +
          "; inspection-sequence=" +
          std::to_string(failure.current_sequence));
    }
    sni_exchange_result origin;
    try {
      origin = lifetime_server.get();
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("dynamic SNI active origin: ") +
                               error.what());
    }
    if (!client.transformed_response ||
        client.peer_leaf != pre_replacement_leaf || !origin.mutual_tls ||
        !origin.transformed_request)
      throw std::runtime_error(
          "replaced SNI broke an already-active TLS session");
  }

  auto replacement_origin = make_listener();
  auto replacement_unused_v6 = make_ipv6_listener();
  auto replacement_server = start_sni_origin(
      replacement_origin, origin_certificate, client_certificate, server_name);
  sni_exchange_result replacement_client;
  {
    auto policy = device.install_tcp_policy(replacement_origin.port,
                                            replacement_unused_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("replaced SNI WFP redirect is missing");
    try {
      replacement_client =
          exchange_sni(AF_INET, replacement_origin.port, server_name);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("dynamic SNI replacement client: ") +
                               error.what());
    }
  }
  sni_exchange_result replacement_server_result;
  try {
    replacement_server_result = replacement_server.get();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("dynamic SNI replacement origin: ") +
                             error.what());
  }
  if (!replacement_client.transformed_response ||
      replacement_client.peer_leaf.empty() ||
      replacement_client.peer_leaf == pre_replacement_leaf ||
      !replacement_server_result.mutual_tls ||
      !replacement_server_result.transformed_request)
    throw std::runtime_error("replacement SNI identity did not serve new TLS");

  auto unknown_origin = make_listener();
  auto unknown_unused_v6 = make_ipv6_listener();
  bool unknown_rejected = false;
  {
    auto policy = device.install_tcp_policy(unknown_origin.port,
                                            unknown_unused_v6.port);
    const auto &evidence = policy.tcp_evidence();
    if (!evidence.filter_id_v4 || !evidence.filter_id_v6)
      throw std::runtime_error("unknown SNI WFP redirect is missing");
    unknown_rejected = rejected_sni(
        AF_INET, unknown_origin.port, "unconfigured.invalid");
  }
  if (!unknown_rejected || has_pending_connection(unknown_origin) ||
      has_pending_connection(unknown_unused_v6))
    throw std::runtime_error("unknown SNI reached the controlled origin");

  const auto complete = [&service](const contract::service_info &value) {
    return value.identity_count == service.identity_count &&
           value.http3_identity_count == service.http3_identity_count &&
           value.accepted >= service.accepted + 3 &&
           value.handshaken >= service.handshaken + 2 &&
           value.origin_connected >= service.origin_connected + 2 &&
           value.origin_completed >= service.origin_completed + 2 &&
           value.origin_peer_validated >=
               service.origin_peer_validated + 2 &&
           value.failed >= service.failed + 1;
  };
  auto after = query_service(device);
  const auto counter_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!complete(after) &&
         std::chrono::steady_clock::now() < counter_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    after = query_service(device);
  }
  if (!complete(after))
    throw std::runtime_error(
        "dynamic SNI counters/lifetime are incomplete: identity=" +
        std::to_string(after.identity_count) + "/" +
        std::to_string(service.identity_count) + ", http3-identity=" +
        std::to_string(after.http3_identity_count) + "/" +
        std::to_string(service.http3_identity_count) + ", accepted-delta=" +
        std::to_string(after.accepted - service.accepted) +
        ", handshaken-delta=" +
        std::to_string(after.handshaken - service.handshaken) +
        ", origin-connected-delta=" +
        std::to_string(after.origin_connected - service.origin_connected) +
        ", origin-completed-delta=" +
        std::to_string(after.origin_completed - service.origin_completed) +
        ", origin-validated-delta=" +
        std::to_string(after.origin_peer_validated -
                       service.origin_peer_validated) +
        ", failed-delta=" +
        std::to_string(after.failed - service.failed));
  return {.second_name = true,
          .replacement = true,
          .observed_peer_leaf_change =
              replacement_client.peer_leaf != pre_replacement_leaf,
          .active_session_lifetime = true,
          .unknown_name_failed_closed = true};
}
