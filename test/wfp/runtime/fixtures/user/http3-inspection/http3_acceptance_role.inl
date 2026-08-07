namespace {

std::filesystem::path current_executable() {
  std::wstring value(32768, L'\0');
  const DWORD size = ::GetModuleFileNameW(
      nullptr, value.data(), static_cast<DWORD>(value.size()));
  if (!size || size == value.size())
    throw std::system_error(::GetLastError(), std::system_category(),
                            "GetModuleFileNameW(user HTTP/3 acceptance)");
  value.resize(size);
  return std::filesystem::canonical(value);
}

std::uint64_t stat_value(std::string_view stats, std::string_view name) {
  const std::string prefix = std::string(name) + "=";
  const auto begin = stats.find(prefix);
  if (begin == std::string_view::npos)
    throw std::runtime_error("HTTP/3 service stats missing " +
                             std::string(name));
  const auto value_begin = begin + prefix.size();
  const auto end = stats.find('\n', value_begin);
  const std::string value(stats.substr(
      value_begin, end == std::string_view::npos ? stats.size() - value_begin
                                                 : end - value_begin));
  char *tail = nullptr;
  const auto parsed = std::strtoull(value.c_str(), &tail, 10);
  if (!tail || *tail)
    throw std::runtime_error("HTTP/3 service stat is not numeric");
  return parsed;
}

class service_process {
public:
  service_process(const std::filesystem::path &service,
                  const std::filesystem::path &ipc_root,
                  std::wstring family, std::wstring scenario,
                  std::wstring policy)
      : ipc_(ipc_root / (family + L"-" + scenario + L"-" + policy + L"-" +
                         std::to_wstring(::GetTickCount64()))),
        process_(service,
                 {current_executable().wstring(),
                  std::to_wstring(::GetCurrentProcessId()),
                  std::move(family), std::move(scenario), std::move(policy),
                  ipc_.wstring()},
                 ipc_) {
    process_.wait_ready();
  }

  std::uint16_t port() const {
    return static_cast<std::uint16_t>(stat_value(process_.stats(), "port"));
  }
  std::string stop() {
    process_.stop();
    return process_.stats();
  }
  void wait_file(std::wstring_view name) {
    process_.wait_file(name);
  }

private:
  std::filesystem::path ipc_;
  crtsys::wfp_test::controller_process process_;
};

void run_ordinary_client(quic_runtime &runtime, int family,
                         service_process &service) {
  ordinary_client_sink client_sink;
  auto connected = backend_connection::try_connect_borrowed(
      runtime.borrowed_native_api(), runtime.borrowed_native_registration(),
      runtime.borrowed_native_client_configuration(),
      "localhost", service.port(), client_sink,
      family == AF_INET ? QUIC_ADDRESS_FAMILY_INET
                        : QUIC_ADDRESS_FAMILY_INET6);
  require(static_cast<bool>(connected), "ordinary client creation failed");
  auto client = std::move(connected).value();
  client_sink.attach(*client);
  require(client_sink.wait_connected(10s),
          "ordinary client handshake timed out");
  client_sink.send_requests();
  require(client_sink.wait_completed(10s),
          "ordinary client codec/QPACK validation timed out");
  client->stop();
  require(client->drain().is_ok(), "ordinary client did not drain");
}

void run_webtransport_client(quic_runtime &runtime, int family,
                             service_process &service, bool blocked) {
  webtransport_sink client_sink(false);
  auto connected = backend_connection::try_connect_borrowed(
      runtime.borrowed_native_api(), runtime.borrowed_native_registration(),
      runtime.borrowed_native_client_configuration(),
      "localhost", service.port(), client_sink,
      family == AF_INET ? QUIC_ADDRESS_FAMILY_INET
                        : QUIC_ADDRESS_FAMILY_INET6);
  require(static_cast<bool>(connected), "WebTransport client creation failed");
  auto client = std::move(connected).value();
  client_sink.attach(client);
  require(client_sink.wait_transport(10s),
          "WebTransport client negotiation timed out");
  require(client_sink.session().send_local_settings(false).is_ok(),
          "WebTransport client SETTINGS failed");
  require(client_sink.wait_peer_settings(10s),
          "WebTransport client did not receive SETTINGS");
  auto &session = client_sink.session();
  ntl::net::http3::webtransport::connect_parameters request{
      .authority = "localhost", .path = "/webtransport",
      .origin = "https://localhost"};
  if (blocked)
    request.additional_headers = {{"x-ntl-block", "1"},
                                  {"x-ntl-block", "0"}};
  require(session.open_client(request).is_ok(),
          "WebTransport Extended CONNECT failed");
  if (blocked) {
    require(client_sink.wait_session_rejected(10s),
            "blocked WebTransport did not return 403");
    const auto forbidden = make_bytes("must-not-be-sent");
    require(!session.active() &&
                session.send_bidirectional(forbidden) ==
                    STATUS_INVALID_DEVICE_STATE &&
                session.send_datagram(forbidden) ==
                    STATUS_INVALID_DEVICE_STATE,
            "rejected WebTransport remained active");
  } else {
    require(client_sink.wait_session_accepted(10s),
            "WebTransport client did not receive 200");
    const auto payload = make_bytes("webtransport-payload");
    require(session.send_bidirectional(payload).is_ok() &&
                session.send_unidirectional(payload).is_ok() &&
                session.send_datagram(payload).is_ok(),
            "WebTransport stream/datagram send failed");
    constexpr std::uint64_t capsule_type = 0x190b4d3;
    require(session.send_capsule(capsule_type, payload).is_ok(),
            "WebTransport capsule send failed");
    constexpr std::uint32_t reset_error = 0x10203040;
    auto reset_stream = session.open_bidirectional_stream();
    require(static_cast<bool>(reset_stream),
            "WebTransport reset stream open failed");
    require(session.write(*reset_stream, payload).is_ok() &&
                session.reset(*reset_stream, reset_error).is_ok() &&
                session.finish().is_ok(),
            "WebTransport reset/finish failed");
    service.wait_file(L"webtransport.complete");
  }
  client->stop();
  require(client->drain().is_ok(), "WebTransport client did not drain");
}

void run_handshake_client(quic_runtime &runtime, int family,
                          service_process &service, bool expect_blocked) {
  ordinary_client_sink sink;
  auto connected = backend_connection::try_connect_borrowed(
      runtime.borrowed_native_api(), runtime.borrowed_native_registration(),
      runtime.borrowed_native_client_configuration(),
      "localhost", service.port(), sink,
      family == AF_INET ? QUIC_ADDRESS_FAMILY_INET
                        : QUIC_ADDRESS_FAMILY_INET6);
  if (connected) {
    auto client = std::move(connected).value();
    sink.attach(*client);
    const bool established = sink.wait_connected(expect_blocked ? 1500ms : 10s);
    client->stop();
    (void)client->drain();
    require(established != expect_blocked,
            expect_blocked ? "unavailable callout allowed handshake"
                           : "direct handshake failed");
  } else {
    require(expect_blocked, "direct client creation failed");
  }
}

} // namespace

int run_controlled_msquic_acceptance(
    const std::filesystem::path &service_executable,
    const std::filesystem::path &ipc_root) {
  try {
    require(webtransport_sink::capsule_reassembly_contract(),
            "Capsule callback/DATA-frame reassembly contract failed");
    quic_runtime runtime;
    unsigned normal_gates = 0;
    unsigned direct_proofs = 0;
    unsigned unavailable_proofs = 0;
    unsigned accepted_connections = 0;
    std::uint64_t wfp_ipv4_delta = 0;
    std::uint64_t wfp_ipv6_delta = 0;
    std::uint64_t application_hash = 0;
    std::uint32_t process_id = 0;
    std::uint16_t original_v4_port = 0;
    std::uint16_t original_v6_port = 0;
    unsigned webtransport_rejected_families = 0;
    auto collect_gate_evidence =
        [&](std::string_view stats, int family) {
          const auto reported_process = stat_value(stats, "process_id");
          const auto reported_hash = stat_value(stats, "app_hash");
          require(reported_process == ::GetCurrentProcessId() &&
                      reported_hash != 0,
                  "service process/application evidence is incomplete");
          if (process_id == 0) {
            process_id = static_cast<std::uint32_t>(reported_process);
            application_hash = reported_hash;
          } else {
            require(process_id == reported_process &&
                        application_hash == reported_hash,
                    "service process/application evidence changed");
          }
          if (family == AF_INET) {
            const auto delta = stat_value(stats, "wfp_ipv4_delta");
            const auto port = stat_value(stats, "original_v4_port");
            require(delta != 0 && port != 0 &&
                        stat_value(stats, "wfp_ipv6_delta") == 0 &&
                        stat_value(stats, "gated_families") == 1,
                    "IPv4 WFP gate evidence is incomplete");
            wfp_ipv4_delta += delta;
            original_v4_port = static_cast<std::uint16_t>(port);
          } else {
            const auto delta = stat_value(stats, "wfp_ipv6_delta");
            const auto port = stat_value(stats, "original_v6_port");
            require(delta != 0 && port != 0 &&
                        stat_value(stats, "wfp_ipv4_delta") == 0 &&
                        stat_value(stats, "gated_families") == 2,
                    "IPv6 WFP gate evidence is incomplete");
            wfp_ipv6_delta += delta;
            original_v6_port = static_cast<std::uint16_t>(port);
          }
          webtransport_rejected_families |= static_cast<unsigned>(
              stat_value(stats, "webtransport_rejected_families"));
        };
    for (const auto &[family_name, family] :
         std::array{std::pair{std::wstring{L"ipv4"}, AF_INET},
                    std::pair{std::wstring{L"ipv6"}, AF_INET6}}) {
      {
        service_process process(service_executable, ipc_root, family_name,
                                L"ordinary", L"normal");
        run_ordinary_client(runtime, family, process);
        const auto stats = process.stop();
        require(stat_value(stats, "ordinary_completed") == 4 &&
                    stat_value(stats, "gate_complete") == 1,
                "ordinary service evidence is incomplete");
        collect_gate_evidence(stats, family);
        ++normal_gates;
        accepted_connections +=
            static_cast<unsigned>(stat_value(stats, "accepted"));
      }
      for (const bool blocked : {false, true}) {
        service_process process(
            service_executable, ipc_root, family_name,
            blocked ? L"webtransport-block" : L"webtransport", L"normal");
        run_webtransport_client(runtime, family, process, blocked);
        const auto stats = process.stop();
        require(stat_value(stats, "gate_complete") == 1 &&
                    stat_value(stats, blocked ? "webtransport_blocked"
                                              : "webtransport_complete") == 1,
                "WebTransport service evidence is incomplete");
        collect_gate_evidence(stats, family);
        ++normal_gates;
        accepted_connections +=
            static_cast<unsigned>(stat_value(stats, "accepted"));
      }
      {
        service_process process(service_executable, ipc_root, family_name,
                                L"handshake", L"direct");
        run_handshake_client(runtime, family, process, false);
        const auto stats = process.stop();
        require(stat_value(stats, "direct_counter_unchanged") == 1,
                "direct policy-removal proof is incomplete");
        require(stat_value(stats, "direct_families") ==
                    (family == AF_INET ? 1u : 2u),
                "direct policy-removal family evidence is incomplete");
        ++direct_proofs;
        accepted_connections +=
            static_cast<unsigned>(stat_value(stats, "accepted"));
      }
      {
        service_process process(service_executable, ipc_root, family_name,
                                L"handshake", L"unavailable");
        run_handshake_client(runtime, family, process, true);
        const auto stats = process.stop();
        require(stat_value(stats, "accepted") == 0 &&
                    stat_value(stats, "unavailable_origin_unchanged") == 1 &&
                    stat_value(stats, "unavailable_families") ==
                        (family == AF_INET ? 1u : 2u),
                "unavailable-callout proof is incomplete");
        ++unavailable_proofs;
      }
    }
    require(normal_gates == 6 && direct_proofs == 2 &&
                unavailable_proofs == 2 && accepted_connections == 8 &&
                wfp_ipv4_delta != 0 && wfp_ipv6_delta != 0 &&
                original_v4_port != 0 && original_v6_port != 0 &&
                process_id != 0 && application_hash != 0 &&
                webtransport_rejected_families == 3,
            "dual-stack service lifecycle evidence is incomplete");
    std::cout << "controlled-msquic-http3: WebTransport PASS ipv4=pass "
                 "ipv6=pass capsule=pass capsule-fragmentation=pass "
                 "webtransport-block=pass inactive-after-403=pass\n"
              << "controlled-msquic-http3: dynamic QPACK and codecs PASS\n"
              << "controlled-msquic-http3: WFP gate PASS "
              << "wfp_ipv4_delta=" << wfp_ipv4_delta << ' '
              << "wfp_ipv6_delta=" << wfp_ipv6_delta << ' '
              << "original_v4_port=" << original_v4_port << ' '
              << "original_v6_port=" << original_v6_port << ' '
              << "process_id=" << process_id << ' '
              << "app_hash=" << application_hash << ' '
              << "tuple=IPv4/IPv6 policy_lifetime=ephemeral "
                 "policy_removed_direct=IPv4/IPv6 counter_unchanged=yes "
                 "unavailable_callout=blocked origin_hit=0 "
                 "webtransport_block=IPv4/IPv6 "
                 "inactive_after_403=yes\n"
              << "raw-msquic-loopback: tls13 settings extended-connect "
                 "webtransport-bidi webtransport-uni h3-datagram capsule "
                 "reliable-reset-at application-error-map dynamic-qpack "
                 "gzip deflate br permit=6 block=2 "
                 "connections=8 malformed=replay-contract PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "controlled-msquic-http3: FAIL: " << error.what() << '\n';
    return 1;
  }
}
