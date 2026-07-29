#include <msquic.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>

#include "test_certificate.hpp"

namespace {

using backend_connection =
    ntl::net::http3::msquic_backend::connection;
using namespace std::chrono_literals;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void require_quic(QUIC_STATUS status, const char *message) {
  if (QUIC_FAILED(status))
    throw std::runtime_error(message);
}

ntl::net::scatter_view as_scatter(
    const std::vector<std::byte> &bytes) noexcept {
  return bytes.empty()
             ? ntl::net::scatter_view{}
             : ntl::net::scatter_view::from_contiguous(
                   std::span<const std::byte>(bytes));
}

std::vector<std::byte> make_extension_message(
    std::uint64_t stream_type,
    std::uint64_t session_id,
    std::string_view payload) {
  std::vector<std::byte> result;
  require(
      ntl::net::http3::append_quic_varint(result, stream_type).is_ok(),
      "cannot encode extension stream type");
  require(
      ntl::net::http3::append_quic_varint(result, session_id).is_ok(),
      "cannot encode WebTransport session id");
  result.insert(
      result.end(),
      reinterpret_cast<const std::byte *>(payload.data()),
      reinterpret_cast<const std::byte *>(
          payload.data() + payload.size()));
  return result;
}

std::vector<std::byte> make_bytes(std::string_view value) {
  return {
      reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(
          value.data() + value.size())};
}

class recording_sink final
    : public ntl::net::http3::quic_backend_sink {
public:
  ntl::status
  on_connected(std::string_view negotiated_alpn) noexcept override {
    {
      std::lock_guard guard(lock_);
      connected_ = negotiated_alpn == "h3";
    }
    changed_.notify_all();
    return connected_ ? ntl::status::ok()
                      : ntl::status{STATUS_PROTOCOL_UNREACHABLE};
  }

  ntl::status on_request_stream(
      std::uint64_t,
      ntl::net::scatter_view,
      bool) noexcept override {
    return STATUS_DATA_ERROR;
  }

  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view bytes) noexcept override {
    return append(bytes, qpack_encoder_);
  }

  ntl::status on_peer_bidirectional_stream(
      std::uint64_t stream_id,
      ntl::net::scatter_view bytes,
      bool final) noexcept override {
    const ntl::status copied = append(bytes, bidirectional_);
    if (!copied.is_ok())
      return copied;
    if (!final)
      return ntl::status::ok();

    backend_connection *echo = nullptr;
    std::vector<std::byte> reply;
    {
      std::lock_guard guard(lock_);
      bidirectional_final_ = true;
      echo = echo_connection_;
      if (echo)
        reply = bidirectional_;
    }
    changed_.notify_all();
    return echo
               ? echo->write_stream(
                     stream_id, as_scatter(reply), true)
               : ntl::status::ok();
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t,
      ntl::net::scatter_view bytes,
      bool final) noexcept override {
    const ntl::status copied = append(bytes, unidirectional_);
    if (!copied.is_ok())
      return copied;
    if (final) {
      {
        std::lock_guard guard(lock_);
        unidirectional_final_ = true;
      }
      changed_.notify_all();
    }
    return ntl::status::ok();
  }

  ntl::status
  on_datagram(ntl::net::scatter_view bytes) noexcept override {
    const ntl::status copied = append(bytes, datagram_);
    if (!copied.is_ok())
      return copied;

    backend_connection *echo = nullptr;
    std::vector<std::byte> reply;
    {
      std::lock_guard guard(lock_);
      datagram_received_ = true;
      echo = echo_datagrams_ ? echo_connection_ : nullptr;
      if (echo)
        reply = datagram_;
    }
    changed_.notify_all();
    return echo ? echo->send_datagram(as_scatter(reply))
                : ntl::status::ok();
  }

  void on_closed(NTSTATUS status) noexcept override {
    {
      std::lock_guard guard(lock_);
      closed_ = true;
      close_status_ = status;
    }
    changed_.notify_all();
  }

  void set_echo_connection(
      backend_connection *connection,
      bool echo_datagrams) noexcept {
    std::lock_guard guard(lock_);
    echo_connection_ = connection;
    echo_datagrams_ = echo_datagrams;
  }

  bool wait_connected(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(
        lock, timeout, [this] { return connected_ || closed_; }) &&
           connected_;
  }

  bool wait_bidirectional(
      const std::vector<std::byte> &expected,
      std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this, &expected] {
      return (bidirectional_final_ &&
              bidirectional_ == expected) ||
             closed_;
    }) &&
           bidirectional_ == expected;
  }

  bool wait_unidirectional(
      const std::vector<std::byte> &expected,
      std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this, &expected] {
      return (unidirectional_final_ &&
              unidirectional_ == expected) ||
             closed_;
    }) &&
           unidirectional_ == expected;
  }

  bool wait_datagram(
      const std::vector<std::byte> &expected,
      std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this, &expected] {
      return (datagram_received_ && datagram_ == expected) ||
             closed_;
    }) &&
           datagram_ == expected;
  }

private:
  ntl::status append(
      ntl::net::scatter_view source,
      std::vector<std::byte> &destination) noexcept {
    std::vector<std::byte> copy(source.size());
    if (!copy.empty()) {
      const ntl::status copied = source.copy_to(copy);
      if (!copied.is_ok())
        return copied;
    }
    try {
      std::lock_guard guard(lock_);
      destination.insert(
          destination.end(), copy.begin(), copy.end());
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  std::mutex lock_;
  std::condition_variable changed_;
  backend_connection *echo_connection_ = nullptr;
  std::vector<std::byte> bidirectional_;
  std::vector<std::byte> unidirectional_;
  std::vector<std::byte> qpack_encoder_;
  std::vector<std::byte> datagram_;
  NTSTATUS close_status_ = STATUS_SUCCESS;
  bool connected_ = false;
  bool bidirectional_final_ = false;
  bool unidirectional_final_ = false;
  bool datagram_received_ = false;
  bool echo_datagrams_ = false;
  bool closed_ = false;
};

struct listener_context {
  const QUIC_API_TABLE *api = nullptr;
  HQUIC configuration = nullptr;
  recording_sink *sink = nullptr;
  std::mutex lock;
  std::condition_variable changed;
  std::unique_ptr<backend_connection> accepted;
  ntl::status accept_status{STATUS_PENDING};
};

QUIC_STATUS QUIC_API listener_callback(
    HQUIC,
    void *context,
    QUIC_LISTENER_EVENT *event) noexcept {
  auto *state = static_cast<listener_context *>(context);
  if (!state || !event)
    return QUIC_STATUS_INVALID_PARAMETER;
  if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
    return QUIC_STATUS_SUCCESS;

  auto accepted = backend_connection::try_accept(
      state->api, event->NEW_CONNECTION.Connection,
      state->configuration, *state->sink);
  {
    std::lock_guard guard(state->lock);
    state->accept_status = accepted.status();
    if (accepted)
      state->accepted = std::move(accepted).value();
  }
  state->changed.notify_all();
  return accepted ? QUIC_STATUS_SUCCESS : QUIC_STATUS_ABORTED;
}

class quic_runtime {
public:
  quic_runtime() {
    require_quic(MsQuicOpen2(&api_), "MsQuicOpen2 failed");

    const QUIC_REGISTRATION_CONFIG registration_config{
        "crtsys-ntl-raw-loopback",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    require_quic(
        api_->RegistrationOpen(
            &registration_config, &registration_),
        "RegistrationOpen failed");

    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount = 16;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 16;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.DatagramReceiveEnabled = TRUE;
    settings.IsSet.DatagramReceiveEnabled = TRUE;
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;

    alpn_.Length = 2;
    alpn_.Buffer =
        reinterpret_cast<std::uint8_t *>(
            const_cast<char *>("h3"));
    require_quic(
        api_->ConfigurationOpen(
            registration_, &alpn_, 1, &settings,
            sizeof(settings), nullptr, &server_configuration_),
        "server ConfigurationOpen failed");
    require_quic(
        api_->ConfigurationOpen(
            registration_, &alpn_, 1, &settings,
            sizeof(settings), nullptr, &client_configuration_),
        "client ConfigurationOpen failed");

    certificate_ =
        std::make_unique<crtsys::wfp_sample::ephemeral_certificate>(
            false);
    QUIC_CREDENTIAL_CONFIG server_credential{};
    server_credential.Type =
        QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
    server_credential.CertificateContext =
        reinterpret_cast<QUIC_CERTIFICATE *>(
            const_cast<CERT_CONTEXT *>(
                certificate_->get()));
    require_quic(
        api_->ConfigurationLoadCredential(
            server_configuration_, &server_credential),
        "server ConfigurationLoadCredential failed");

    QUIC_CREDENTIAL_CONFIG client_credential{};
    client_credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
    client_credential.Flags =
        static_cast<QUIC_CREDENTIAL_FLAGS>(
            QUIC_CREDENTIAL_FLAG_CLIENT |
            QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION);
    require_quic(
        api_->ConfigurationLoadCredential(
            client_configuration_, &client_credential),
        "client ConfigurationLoadCredential failed");
  }

  quic_runtime(const quic_runtime &) = delete;
  quic_runtime &operator=(const quic_runtime &) = delete;

  ~quic_runtime() {
    if (client_configuration_)
      api_->ConfigurationClose(client_configuration_);
    if (server_configuration_)
      api_->ConfigurationClose(server_configuration_);
    certificate_.reset();
    if (registration_)
      api_->RegistrationClose(registration_);
    if (api_)
      MsQuicClose(api_);
  }

  const QUIC_API_TABLE *api() const noexcept { return api_; }
  HQUIC registration() const noexcept { return registration_; }
  HQUIC server_configuration() const noexcept {
    return server_configuration_;
  }
  HQUIC client_configuration() const noexcept {
    return client_configuration_;
  }
  const QUIC_BUFFER &alpn() const noexcept { return alpn_; }

private:
  const QUIC_API_TABLE *api_ = nullptr;
  HQUIC registration_ = nullptr;
  HQUIC server_configuration_ = nullptr;
  HQUIC client_configuration_ = nullptr;
  QUIC_BUFFER alpn_{};
  std::unique_ptr<crtsys::wfp_sample::ephemeral_certificate>
      certificate_;
};

int run_test() {
  quic_runtime runtime;
  recording_sink server_sink;
  recording_sink client_sink;

  listener_context listener_state{
      .api = runtime.api(),
      .configuration = runtime.server_configuration(),
      .sink = &server_sink};
  HQUIC listener = nullptr;
  require_quic(
      runtime.api()->ListenerOpen(
          runtime.registration(), &listener_callback,
          &listener_state, &listener),
      "ListenerOpen failed");

  QUIC_ADDR address{};
  QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_INET);
  QuicAddrSetToLoopback(&address);
  QuicAddrSetPort(&address, 0);
  require_quic(
      runtime.api()->ListenerStart(
          listener, &runtime.alpn(), 1, &address),
      "ListenerStart failed");

  std::uint32_t address_size = sizeof(address);
  require_quic(
      runtime.api()->GetParam(
          listener, QUIC_PARAM_LISTENER_LOCAL_ADDRESS,
          &address_size, &address),
      "listener local-address query failed");
  const std::uint16_t port = QuicAddrGetPort(&address);
  require(port != 0, "listener did not select a UDP port");

  auto client_result = backend_connection::try_connect(
      runtime.api(), runtime.registration(),
      runtime.client_configuration(), "localhost", port,
      client_sink, QUIC_ADDRESS_FAMILY_INET);
  require(
      static_cast<bool>(client_result),
      "raw MsQuic client creation failed");
  auto client = std::move(client_result).value();

  {
    std::unique_lock lock(listener_state.lock);
    require(
        listener_state.changed.wait_for(
            lock, 10s, [&listener_state] {
              return listener_state.accepted ||
                     listener_state.accept_status !=
                         STATUS_PENDING;
            }),
        "server accept timed out");
    require(
        static_cast<bool>(listener_state.accepted),
        "raw MsQuic server accept failed");
  }
  backend_connection *server = listener_state.accepted.get();
  server_sink.set_echo_connection(server, true);

  require(
      server_sink.wait_connected(10s),
      "server TLS 1.3 handshake timed out");
  require(
      client_sink.wait_connected(10s),
      "client TLS 1.3 handshake timed out");

  const auto bidirectional = make_extension_message(
      ntl::net::http3::msquic_backend::
          webtransport_bidirectional_signal,
      7, "bidirectional-payload");
  std::uint64_t bidirectional_id = 0;
  require(
      client->open_bidirectional_stream(
                  bidirectional_id)
          .is_ok(),
      "cannot open bidirectional QUIC stream");
  require(
      client
          ->write_stream(
              bidirectional_id, as_scatter(bidirectional), true)
          .is_ok(),
      "cannot send bidirectional QUIC stream");
  require(
      server_sink.wait_bidirectional(bidirectional, 10s),
      "server did not receive the WebTransport bidi stream");
  require(
      client_sink.wait_bidirectional(bidirectional, 10s),
      "client did not receive the echoed bidi stream");

  const auto unidirectional =
      make_extension_message(0x54, 7, "unidirectional-payload");
  std::uint64_t unidirectional_id = 0;
  require(
      client->open_unidirectional_stream(
                  unidirectional_id)
          .is_ok(),
      "cannot open unidirectional QUIC stream");
  require(
      client
          ->write_stream(
              unidirectional_id, as_scatter(unidirectional),
              true)
          .is_ok(),
      "cannot send unidirectional QUIC stream");
  require(
      server_sink.wait_unidirectional(unidirectional, 10s),
      "server did not receive the WebTransport uni stream");

  const auto datagram = make_bytes("quic-datagram-payload");
  require(
      client->send_datagram(as_scatter(datagram)).is_ok(),
      "cannot send QUIC DATAGRAM");
  require(
      server_sink.wait_datagram(datagram, 10s),
      "server did not receive QUIC DATAGRAM");
  require(
      client_sink.wait_datagram(datagram, 10s),
      "client did not receive the echoed QUIC DATAGRAM");

  client->stop();
  server->stop();
  require(
      client->wait_for_shutdown(10s),
      "client connection shutdown timed out");
  require(
      server->wait_for_shutdown(10s),
      "server connection shutdown timed out");

  client.reset();
  listener_state.accepted.reset();
  runtime.api()->ListenerClose(listener);
  std::cout
      << "raw-msquic-loopback: tls13 bidi uni datagram PASS\n";
  return 0;
}

} // namespace

int main() {
  try {
    return run_test();
  } catch (const std::exception &error) {
    std::cerr << "raw-msquic-loopback: FAIL: "
              << error.what() << '\n';
    return 1;
  }
}
