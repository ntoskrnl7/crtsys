#include <msquic.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ntl/net/http3/msquic_runtime>
#include <ntl/net/http3/msquic_server>
#include <ntl/net/http3/proxy_connection>

#include "controller_lifecycle.hpp"
#include "http3_inspection_policy.hpp"
#include "test_certificate.hpp"
#include "wfp_gate.hpp"

using crtsys::wfp_user_http3::gate_controller;
using crtsys::wfp_user_http3::gate_evidence;
using crtsys::wfp_user_http3::gate_policy;

namespace {

using namespace std::chrono_literals;
namespace backend = ntl::net::http3::msquic_backend;
namespace webtransport = ntl::net::http3::webtransport;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void require_status(ntl::status value, const char *message) {
  if (!value.is_ok())
    throw std::runtime_error(message);
}

int parse_family(std::wstring_view value) {
  if (value == L"ipv4")
    return AF_INET;
  if (value == L"ipv6")
    return AF_INET6;
  throw std::invalid_argument("family must be ipv4 or ipv6");
}

std::uint32_t parse_process_id(std::wstring_view value) {
  const std::wstring owned(value);
  wchar_t *end = nullptr;
  const auto parsed = std::wcstoul(owned.c_str(), &end, 10);
  if (!end || *end || parsed == 0 || parsed > MAXDWORD)
    throw std::invalid_argument("controlled process id is invalid");
  return static_cast<std::uint32_t>(parsed);
}

QUIC_ADDRESS_FAMILY quic_family(int family) noexcept {
  return family == AF_INET ? QUIC_ADDRESS_FAMILY_INET
                           : QUIC_ADDRESS_FAMILY_INET6;
}

std::string service_stats(
    std::uint16_t port, std::uint64_t accepted,
    std::size_t ordinary_completed, bool webtransport_complete,
    bool blocked_complete, bool gate_complete, bool direct_unchanged,
    bool unavailable_unchanged, const gate_evidence &evidence) {
  std::ostringstream out;
  out << "state=stopped\n"
      << "port=" << port << '\n'
      << "accepted=" << accepted << '\n'
      << "ordinary_completed=" << ordinary_completed << '\n'
      << "webtransport_complete=" << (webtransport_complete ? 1 : 0)
      << '\n'
      << "webtransport_blocked=" << (blocked_complete ? 1 : 0) << '\n'
      << "gate_complete=" << (gate_complete ? 1 : 0) << '\n'
      << "direct_counter_unchanged=" << (direct_unchanged ? 1 : 0)
      << '\n'
      << "unavailable_origin_unchanged="
      << (unavailable_unchanged ? 1 : 0) << '\n'
      << "wfp_ipv4_delta=" << evidence.ipv4_delta << '\n'
      << "wfp_ipv6_delta=" << evidence.ipv6_delta << '\n'
      << "original_v4_port=" << evidence.original_v4_port << '\n'
      << "original_v6_port=" << evidence.original_v6_port << '\n'
      << "process_id=" << evidence.process_id << '\n'
      << "app_hash=" << evidence.application_hash << '\n'
      << "gated_families=" << evidence.gated_families << '\n'
      << "direct_families=" << evidence.direct_families << '\n'
      << "unavailable_families=" << evidence.unavailable_families << '\n'
      << "webtransport_rejected_families="
      << evidence.webtransport_rejected_families << '\n';
  return out.str();
}

class service_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  void on_connected(std::string_view alpn) noexcept override {
    {
      std::lock_guard guard(lock_);
      connected_ = alpn == "h3";
    }
    changed_.notify_all();
  }

  void on_exchange_complete(
      std::uint64_t, const ntl::net::http::request_message &request,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override {
    {
      std::lock_guard guard(lock_);
      ++completed_;
      const bool webtransport_route =
          request.method == "CONNECT" && request.path == "/webtransport";
      if (terminal && webtransport_route && response.status == 403)
        webtransport_blocked_ = true;
    }
    changed_.notify_all();
  }

  void on_webtransport_session_opened(std::uint64_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      webtransport_opened_ = true;
    }
    changed_.notify_all();
  }

  void on_webtransport_payload(
      const webtransport::payload &payload) noexcept override {
    constexpr std::string_view expected = "webtransport-payload";
    const std::string_view bytes(
        reinterpret_cast<const char *>(payload.bytes.data()),
        payload.bytes.size());
    {
      std::lock_guard guard(lock_);
      if (bytes != expected) {
        invalid_webtransport_payload_ = true;
      } else if (payload.kind == webtransport::payload_kind::datagram) {
        webtransport_datagram_ = true;
      } else if (payload.kind == webtransport::payload_kind::capsule) {
        webtransport_capsule_ = payload.capsule_type == 0x190b4d3;
      } else if (payload.direction ==
                 webtransport::stream_direction::bidirectional) {
        webtransport_bidirectional_ = true;
      } else {
        webtransport_unidirectional_ = true;
      }
    }
    changed_.notify_all();
  }

  void on_webtransport_reset(
      std::uint64_t, std::uint32_t error) noexcept override {
    {
      std::lock_guard guard(lock_);
      webtransport_reset_ = error == 0x10203040;
    }
    changed_.notify_all();
  }

  void on_closed(NTSTATUS) noexcept override {
    {
      std::lock_guard guard(lock_);
      closed_ = true;
    }
    changed_.notify_all();
  }

  bool wait_connected(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(
               lock, timeout,
               [this] { return connected_ || closed_; }) &&
           connected_;
  }

  bool wait_completed(
      std::size_t count, std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(
               lock, timeout,
               [this, count] { return completed_ >= count || closed_; }) &&
           completed_ >= count;
  }

  bool wait_webtransport_blocked(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(
               lock, timeout,
               [this] { return webtransport_blocked_ || closed_; }) &&
           webtransport_blocked_;
  }

  bool wait_webtransport_complete(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    const auto complete = [this] {
      return invalid_webtransport_payload_ || closed_ ||
             (webtransport_opened_ && webtransport_bidirectional_ &&
              webtransport_unidirectional_ && webtransport_datagram_ &&
              webtransport_capsule_ && webtransport_reset_);
    };
    return changed_.wait_for(lock, timeout, complete) &&
           !invalid_webtransport_payload_ && webtransport_opened_ &&
           webtransport_bidirectional_ && webtransport_unidirectional_ &&
           webtransport_datagram_ && webtransport_capsule_ &&
           webtransport_reset_;
  }

  std::string webtransport_evidence() {
    std::lock_guard guard(lock_);
    std::ostringstream out;
    out << "opened=" << webtransport_opened_
        << " bidi=" << webtransport_bidirectional_
        << " uni=" << webtransport_unidirectional_
        << " datagram=" << webtransport_datagram_
        << " capsule=" << webtransport_capsule_
        << " reset=" << webtransport_reset_
        << " invalid_payload=" << invalid_webtransport_payload_
        << " closed=" << closed_;
    return out.str();
  }

private:
  std::mutex lock_;
  std::condition_variable changed_;
  std::size_t completed_ = 0;
  bool connected_ = false;
  bool closed_ = false;
  bool webtransport_blocked_ = false;
  bool webtransport_opened_ = false;
  bool webtransport_bidirectional_ = false;
  bool webtransport_unidirectional_ = false;
  bool webtransport_datagram_ = false;
  bool webtransport_capsule_ = false;
  bool webtransport_reset_ = false;
  bool invalid_webtransport_payload_ = false;
};

ntl::net::http3::proxy_connection_limits proxy_limits() noexcept {
  return {
      .maximum_concurrent_request_streams = 8,
      .maximum_buffered_bytes_per_stream = 64 * 1024,
      .maximum_aggregate_body_bytes = 256 * 1024,
      .maximum_frame_payload = 64 * 1024,
      .maximum_decoded_header_bytes = 32 * 1024,
      .maximum_control_stream_bytes = 4096,
      .maximum_extension_stream_bytes = 64 * 1024,
      .maximum_capsule_wire_bytes = 64 * 1024,
      .maximum_blocked_streams = 8,
      .maximum_concurrent_webtransport_sessions = 8,
      .qpack_table_capacity = 256,
      .require_http3_origin = true,
      .enable_webtransport = true};
}

webtransport::session_limits webtransport_limits() noexcept {
  return {.maximum_bidirectional_streams = 8,
          .maximum_unidirectional_streams = 8,
          .maximum_stream_data = 64 * 1024,
          .maximum_datagram_payload = 4096,
          .maximum_datagrams = 32};
}

} // namespace

int run_user_http3_service(int argc, wchar_t **argv) {
  try {
    if (argc != 7)
      throw std::invalid_argument(
          "usage: crtsys_wfp_http3_inspection_service.exe "
          "<controlled-app.exe> <controlled-pid> <ipv4|ipv6> "
          "<ordinary|webtransport|webtransport-block|handshake> "
          "<normal|direct|unavailable> <ipc-directory>");

    const auto application = std::filesystem::canonical(argv[1]);
    const auto process_id = parse_process_id(argv[2]);
    const int family = parse_family(argv[3]);
    const std::wstring scenario(argv[4]);
    const std::wstring policy_mode(argv[5]);
    crtsys::wfp_sample::controller_lifecycle lifecycle(argv[6]);
    gate_controller gate(application, process_id);

    const bool webtransport_scenario =
        scenario == L"webtransport" || scenario == L"webtransport-block";
    if (!webtransport_scenario && scenario != L"ordinary" &&
        scenario != L"handshake")
      throw std::invalid_argument("unknown HTTP/3 service scenario");
    if (policy_mode != L"normal" && policy_mode != L"direct" &&
        policy_mode != L"unavailable")
      throw std::invalid_argument("unknown HTTP/3 service policy mode");

    auto policy =
        crtsys::examples::wfp::http3_inspection::make_ordinary_policy();
    auto origin = std::make_shared<
        crtsys::examples::wfp::http3_inspection::ordinary_origin>(
        policy->content_encoders());
    auto async_origin = std::make_shared<
        ntl::net::http3::immediate_origin_transport_adapter>(origin);
    auto terminals = std::make_shared<
        crtsys::examples::wfp::http3_inspection::
            ordinary_terminal_responses>();
    auto observer = std::make_shared<service_observer>();
    auto payload_policy =
        crtsys::examples::wfp::http3_inspection::
            make_webtransport_policy();
    auto payload_handler = std::make_shared<
        ntl::net::http3::webtransport_echo_handler>();

    // This controller is a long-running WFP service and can be launched
    // without an interactive user profile. Keep its ephemeral private key in
    // machine scope; the owning certificate removes the unique container.
    crtsys::wfp_sample::ephemeral_certificate certificate(true);
    backend::server listener;
    backend::configuration configuration;
    backend::runtime runtime;

    require_status(runtime.open("crtsys-ntl-http3-inspection"),
                   "MsQuic runtime open failed");
    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount = 16;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 16;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.DatagramReceiveEnabled = TRUE;
    settings.IsSet.DatagramReceiveEnabled = TRUE;
#ifdef QUIC_API_ENABLE_PREVIEW_FEATURES
    settings.ReliableResetEnabled = TRUE;
    settings.IsSet.ReliableResetEnabled = TRUE;
#endif
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    constexpr std::array<std::string_view, 1> protocols{"h3"};
    require_status(configuration.open(runtime, protocols, &settings),
                   "MsQuic server configuration open failed");

    QUIC_CREDENTIAL_CONFIG credential{};
    credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
    credential.CertificateContext = reinterpret_cast<QUIC_CERTIFICATE *>(
        const_cast<CERT_CONTEXT *>(certificate.get()));
    require_status(configuration.load_credential(credential),
                   "MsQuic server credential load failed");

    const std::string application_name = application.string();
    require_status(
        listener.open(
            runtime,
            [configuration](const backend::accepted_connection_info_view &)
                -> ntl::result<backend::configuration> {
              return ntl::ok(configuration);
            },
            [async_origin, policy, process_id, application_name, observer,
             payload_policy, payload_handler, terminals](
                std::shared_ptr<ntl::net::quic::transport_backend> transport,
                const backend::accepted_connection_info_view &information)
                -> ntl::result<std::shared_ptr<
                    ntl::net::quic::backend_sink>> {
              ntl::net::http::inspection_session_metadata session;
              session.connection.process_id = process_id;
              session.connection.application_label = application_name;
              session.tls.server_name = information.server_name.empty()
                                            ? "localhost"
                                            : std::string(
                                                  information.server_name);
              session.tls.alpn = "h3";
              auto proxy = ntl::net::http3::proxy_connection::create(
                  std::move(transport), async_origin, policy,
                  std::move(session), observer, payload_policy,
                  payload_handler, proxy_limits(), webtransport_limits(),
                  terminals);
              return proxy
                         ? ntl::ok(std::static_pointer_cast<
                               ntl::net::quic::backend_sink>(
                               std::move(proxy).value()))
                         : ntl::result<std::shared_ptr<
                               ntl::net::quic::backend_sink>>(
                               ntl::unexpected(proxy.status()));
            },
            {.maximum_active_connections = 8,
             .connection = {},
             .shutdown_timeout = 20s}),
        "MsQuic HTTP/3 listener open failed");

    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, quic_family(family));
    QuicAddrSetToLoopback(&address);
    QuicAddrSetPort(&address, 0);
    require_status(listener.start(protocols, &address),
                   "MsQuic HTTP/3 listener start failed");
    auto local_address = listener.local_address();
    require(static_cast<bool>(local_address),
            "MsQuic HTTP/3 listener address query failed");
    const auto port = QuicAddrGetPort(&*local_address);
    require(port != 0, "MsQuic HTTP/3 listener selected no UDP port");

    const auto counter_before = gate.snapshot();
    std::optional<gate_policy> installed_policy;
    if (policy_mode == L"normal")
      installed_policy.emplace(gate.install(port));
    else if (policy_mode == L"unavailable")
      installed_policy.emplace(gate.install_unavailable(port));

    lifecycle.publish_ready("state=ready\nport=" +
                            std::to_string(port) + "\n");
    bool webtransport_complete = false;
    bool blocked_complete = false;
    std::size_t ordinary_completed = 0;
    if (policy_mode != L"unavailable") {
      require(observer->wait_connected(20s),
              "service HTTP/3 handshake timed out");
      if (scenario == L"webtransport-block") {
        blocked_complete = observer->wait_webtransport_blocked(10s);
        require(blocked_complete,
                "service did not reject blocked WebTransport");
        gate.record_webtransport_rejection(family);
      } else if (scenario == L"webtransport") {
        webtransport_complete =
            observer->wait_webtransport_complete(10s);
        if (!webtransport_complete)
          throw std::runtime_error(
              "service WebTransport payload evidence is incomplete: " +
              observer->webtransport_evidence());
        lifecycle.acknowledge(L"webtransport.complete");
      } else if (scenario == L"ordinary") {
        require(observer->wait_completed(4, 20s),
                "service did not inspect four ordinary requests");
        ordinary_completed = 4;
      }
    }

    lifecycle.wait_for_stop();
    require_status(listener.drain(20s),
                   "MsQuic HTTP/3 listener did not drain");
    const auto listener_statistics = listener.statistics();

    bool gate_complete = false;
    bool direct_unchanged = false;
    bool unavailable_unchanged = false;
    if (policy_mode == L"normal") {
      gate.verify_gate(*installed_policy, family);
      gate_complete = true;
    } else if (policy_mode == L"direct") {
      gate.verify_direct_after_removal(
          family, counter_before, gate.snapshot());
      direct_unchanged = true;
    } else {
      gate.verify_unavailable(
          *installed_policy, family, listener_statistics.accepted);
      unavailable_unchanged = true;
    }

    listener.close();
    lifecycle.publish_stats(service_stats(
        port, listener_statistics.accepted, ordinary_completed,
        webtransport_complete, blocked_complete, gate_complete,
        direct_unchanged, unavailable_unchanged, gate.evidence()));
    return 0;
  } catch (const std::exception &error) {
    if (argc >= 7) {
      try {
        std::ofstream failure(
            std::filesystem::path(argv[6]) / L"controller.error",
            std::ios::binary | std::ios::trunc);
        failure << error.what();
      } catch (...) {
      }
    }
    std::cerr << "user HTTP/3 inspection service failed: "
              << error.what() << '\n';
    return 1;
  }
}
