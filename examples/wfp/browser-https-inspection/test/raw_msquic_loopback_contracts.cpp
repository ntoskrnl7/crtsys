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
#include <unordered_map>
#include <utility>
#include <vector>

#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/http3/webtransport_transform>

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

class webtransport_sink final
    : public ntl::net::http3::quic_backend_sink {
public:
  explicit webtransport_sink(bool server) : server_(server) {
    transform_.inspect(
        [](const ntl::net::http3::webtransport::payload &) {
          return ntl::net::inspection::verdict::permit;
        });
  }

  void attach(backend_connection &connection) {
    std::lock_guard guard(lock_);
    connection_ = &connection;
    session_ = std::make_unique<
        ntl::net::http3::webtransport::backend_session>(connection);
  }

  ntl::net::http3::webtransport::backend_session &session() {
    std::lock_guard guard(lock_);
    require(session_ != nullptr, "WebTransport session is not attached");
    session_->set_negotiated_transport(
        {datagram_send_enabled_, reliable_reset_negotiated_});
    return *session_;
  }

  ntl::status on_connected(std::string_view alpn) noexcept override {
    {
      std::lock_guard guard(lock_);
      connected_ = alpn == "h3";
    }
    changed_.notify_all();
    return connected_ ? ntl::status::ok()
                      : ntl::status{STATUS_PROTOCOL_UNREACHABLE};
  }

  ntl::status on_request_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    std::vector<std::byte> buffered;
    try {
      std::lock_guard guard(lock_);
      auto &wire = request_wires_[stream_id];
      if (!append(bytes, wire))
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered = wire;
      if (headers_seen_)
        return final ? ntl::status{STATUS_DATA_ERROR} : ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    const auto wire = as_scatter(buffered);
    ntl::net::http3::frame_framer framer({64 * 1024});
    const auto probe = framer.probe(wire);
    if (probe.state() == ntl::net::framing::probe_state::need_more)
      return final ? ntl::status{STATUS_END_OF_FILE} : ntl::status::ok();
    if (probe.state() != ntl::net::framing::probe_state::complete)
      return probe.error();
    auto frame_wire = wire.subview(0, probe.frame_size());
    if (!frame_wire)
      return frame_wire.status();
    const auto frame = ntl::net::http3::frame_view::parse(*frame_wire,
                                                          {64 * 1024});
    if (!frame || frame->header().type() !=
                      ntl::net::http3::frame_type::headers)
      return frame ? ntl::status{STATUS_DATA_ERROR} : frame.status();
    ntl::net::http3::bounded_static_qpack_decoder decoder;
    const auto headers = decoder.decode(stream_id, frame->payload(),
                                        64 * 1024);
    if (!headers)
      return headers.status();

    if (server_) {
      const ntl::net::http3::webtransport::prerequisites prerequisites{
          true, true, true, true, true, true, true, true};
      const auto request =
          ntl::net::http3::webtransport::validate_session_request(
              std::span<const ntl::net::http3::header_field>(headers->fields),
              prerequisites);
      if (!request)
        return request.status();
      ntl::net::http3::webtransport::backend_session *session = nullptr;
      {
        std::lock_guard guard(lock_);
        if (!peer_settings_ready_ || !peer_settings_.client_ready() ||
            !datagram_send_enabled_ || !reliable_reset_negotiated_ ||
            !session_)
          return STATUS_NOT_SUPPORTED;
        headers_seen_ = true;
        connect_stream_id_ = stream_id;
        request_wires_.erase(stream_id);
        session = session_.get();
        session->set_negotiated_transport({true, true});
      }
      const ntl::status accepted = session->accept_server(stream_id);
      if (!accepted.is_ok())
        return accepted;
    } else {
      if (headers->fields.size() != 1 ||
          headers->fields[0].name != ":status" ||
          headers->fields[0].value != "200")
        return STATUS_DATA_ERROR;
      {
        std::lock_guard guard(lock_);
        headers_seen_ = true;
        session_accepted_ = true;
        connect_stream_id_ = stream_id;
        request_wires_.erase(stream_id);
      }
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view bytes) noexcept override {
    return bytes ? ntl::status{STATUS_NOT_SUPPORTED} : ntl::status::ok();
  }

  ntl::status on_peer_bidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    std::vector<std::byte> buffered;
    try {
      std::lock_guard guard(lock_);
      auto &wire = bidirectional_wires_[stream_id];
      if (!append(bytes, wire))
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered = wire;
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!final)
      return ntl::status::ok();
    const auto parsed =
        ntl::net::http3::webtransport::parse_stream_prefix(
            ntl::net::http3::webtransport::stream_direction::bidirectional,
            as_scatter(buffered));
    if (!parsed)
      return parsed.status();
    ntl::net::http3::webtransport::payload semantic{
        ntl::net::http3::webtransport::payload_kind::stream,
        parsed->session_id,
        ntl::net::http3::webtransport::stream_direction::bidirectional,
        0, std::vector<std::byte>(parsed->body.size())};
    if (!semantic.bytes.empty() &&
        !parsed->body.copy_to(semantic.bytes).is_ok())
      return STATUS_DATA_ERROR;
    std::lock_guard transform_guard(transform_lock_);
    auto accounted = transform_.open_stream(
        ntl::net::http3::webtransport::stream_direction::bidirectional);
    if (!accounted.is_ok())
      return accounted;
    const auto transformed = transform_.apply(semantic);
    if (transformed.action !=
            ntl::net::http3::webtransport::transform_action::forward ||
        transformed.failure != STATUS_SUCCESS)
      return transformed.failure == STATUS_SUCCESS
                 ? ntl::status{STATUS_ACCESS_DENIED}
                 : ntl::status{transformed.failure};
    {
      std::lock_guard guard(lock_);
      bidirectional_payload_ = std::move(semantic.bytes);
      bidirectional_session_id_ = parsed->session_id;
      bidirectional_ready_ = true;
      bidirectional_wires_.erase(stream_id);
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    std::vector<std::byte> buffered;
    try {
      std::lock_guard guard(lock_);
      auto &wire = unidirectional_wires_[stream_id];
      if (!append(bytes, wire))
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered = wire;
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    const auto wire = as_scatter(buffered);
    const auto type = ntl::net::http3::read_quic_varint(wire);
    if (!type)
      return !final && type.status() == STATUS_BUFFER_TOO_SMALL
                 ? ntl::status::ok()
                 : type.status();
    if (type->value ==
        ntl::net::http3::webtransport::control_stream_type) {
      const auto settings =
          ntl::net::http3::webtransport::parse_control_stream(wire);
      if (!settings) {
        if (!final && (settings.status() == STATUS_BUFFER_TOO_SMALL ||
                       settings.status() == STATUS_END_OF_FILE))
          return ntl::status::ok();
        return settings.status();
      }
      {
        std::lock_guard guard(lock_);
        peer_settings_ = *settings;
      peer_settings_ready_ = true;
      }
      changed_.notify_all();
      return ntl::status::ok();
    }
    if (!final)
      return ntl::status::ok();
    const auto parsed =
        ntl::net::http3::webtransport::parse_stream_prefix(
            ntl::net::http3::webtransport::stream_direction::unidirectional,
            wire);
    if (!parsed)
      return parsed.status();
    ntl::net::http3::webtransport::payload semantic{
        ntl::net::http3::webtransport::payload_kind::stream,
        parsed->session_id,
        ntl::net::http3::webtransport::stream_direction::unidirectional,
        0, std::vector<std::byte>(parsed->body.size())};
    if (!semantic.bytes.empty() &&
        !parsed->body.copy_to(semantic.bytes).is_ok())
      return STATUS_DATA_ERROR;
    std::lock_guard transform_guard(transform_lock_);
    auto accounted = transform_.open_stream(
        ntl::net::http3::webtransport::stream_direction::unidirectional);
    if (!accounted.is_ok())
      return accounted;
    const auto transformed = transform_.apply(semantic);
    if (transformed.action !=
            ntl::net::http3::webtransport::transform_action::forward ||
        transformed.failure != STATUS_SUCCESS)
      return transformed.failure == STATUS_SUCCESS
                 ? ntl::status{STATUS_ACCESS_DENIED}
                 : ntl::status{transformed.failure};
    {
      std::lock_guard guard(lock_);
      unidirectional_payload_ = std::move(semantic.bytes);
      unidirectional_session_id_ = parsed->session_id;
      unidirectional_ready_ = true;
      unidirectional_wires_.erase(stream_id);
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_datagram(ntl::net::scatter_view bytes) noexcept override {
    const auto datagram = ntl::net::http::http3_datagram_view::parse(bytes);
    if (!datagram)
      return datagram.status();
    ntl::net::http3::webtransport::payload semantic{
        ntl::net::http3::webtransport::payload_kind::datagram,
        datagram->request_stream_id(),
        ntl::net::http3::webtransport::stream_direction::bidirectional,
        0, std::vector<std::byte>(datagram->payload().size())};
    if (!semantic.bytes.empty() &&
        !datagram->payload().copy_to(semantic.bytes).is_ok())
      return STATUS_DATA_ERROR;
    std::lock_guard transform_guard(transform_lock_);
    const auto transformed = transform_.apply(semantic);
    if (transformed.action !=
            ntl::net::http3::webtransport::transform_action::forward ||
        transformed.failure != STATUS_SUCCESS)
      return transformed.failure == STATUS_SUCCESS
                 ? ntl::status{STATUS_ACCESS_DENIED}
                 : ntl::status{transformed.failure};
    {
      std::lock_guard guard(lock_);
      datagram_payload_ = std::move(semantic.bytes);
      datagram_session_id_ = datagram->request_stream_id();
      datagram_ready_ = true;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_peer_send_aborted(
      std::uint64_t stream_id,
      std::uint64_t error_code) noexcept override {
    std::vector<std::byte> buffered;
    try {
      std::lock_guard guard(lock_);
      const auto found = bidirectional_wires_.find(stream_id);
      if (found == bidirectional_wires_.end())
        return STATUS_NOT_FOUND;
      buffered = found->second;
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    const auto parsed =
        ntl::net::http3::webtransport::parse_stream_prefix(
            ntl::net::http3::webtransport::stream_direction::bidirectional,
            as_scatter(buffered));
    const auto application_error =
        ntl::net::http3::webtransport::application_error_from_http3(
            error_code);
    if (!parsed || !application_error)
      return STATUS_DATA_ERROR;
    {
      std::lock_guard guard(lock_);
      reset_session_id_ = parsed->session_id;
      reset_application_error_ = *application_error;
      reset_ready_ = true;
      bidirectional_wires_.erase(stream_id);
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  void on_datagram_send_state(bool enabled,
                              std::size_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      datagram_send_enabled_ = enabled;
    }
    changed_.notify_all();
  }

  void on_reliable_reset_negotiated(bool enabled) noexcept override {
    {
      std::lock_guard guard(lock_);
      reliable_reset_negotiated_ = enabled;
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

  bool wait_transport(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this] {
      return (connected_ && datagram_send_enabled_ &&
              reliable_reset_negotiated_) ||
             closed_;
    }) && connected_ && datagram_send_enabled_ &&
           reliable_reset_negotiated_;
  }

  bool wait_peer_settings(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this] {
      return peer_settings_ready_ || closed_;
    }) && peer_settings_ready_;
  }

  bool wait_session_accepted(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this] {
      return session_accepted_ || closed_;
    }) && session_accepted_;
  }

  bool wait_payloads(std::span<const std::byte> expected,
                     std::uint64_t session_id,
                     std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    const auto ready = changed_.wait_for(lock, timeout, [this] {
      return (bidirectional_ready_ && unidirectional_ready_ &&
              datagram_ready_) ||
             closed_;
    });
    const std::vector<std::byte> expected_vector(expected.begin(),
                                                  expected.end());
    return ready && bidirectional_ready_ && unidirectional_ready_ &&
           datagram_ready_ && bidirectional_session_id_ == session_id &&
           unidirectional_session_id_ == session_id &&
           datagram_session_id_ == session_id &&
           bidirectional_payload_ == expected_vector &&
           unidirectional_payload_ == expected_vector &&
           datagram_payload_ == expected_vector;
  }

  bool wait_reset(std::uint64_t session_id,
                  std::uint32_t application_error,
                  std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this] {
      return reset_ready_ || closed_;
    }) && reset_ready_ && reset_session_id_ == session_id &&
           reset_application_error_ == application_error;
  }

private:
  static bool append(ntl::net::scatter_view source,
                     std::vector<std::byte> &destination) noexcept {
    try {
      std::vector<std::byte> copied(source.size());
      if (!copied.empty() && !source.copy_to(copied).is_ok())
        return false;
      destination.insert(destination.end(), copied.begin(), copied.end());
      return true;
    } catch (...) {
      return false;
    }
  }

  bool server_ = false;
  std::mutex lock_;
  std::mutex transform_lock_;
  std::condition_variable changed_;
  backend_connection *connection_ = nullptr;
  std::unique_ptr<ntl::net::http3::webtransport::backend_session> session_;
  ntl::net::http3::webtransport::transform_session transform_;
  ntl::net::http3::webtransport::peer_settings peer_settings_{};
  std::unordered_map<std::uint64_t, std::vector<std::byte>> request_wires_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>>
      bidirectional_wires_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>>
      unidirectional_wires_;
  std::vector<std::byte> bidirectional_payload_;
  std::vector<std::byte> unidirectional_payload_;
  std::vector<std::byte> datagram_payload_;
  std::uint64_t connect_stream_id_ = 0;
  std::uint64_t bidirectional_session_id_ = 0;
  std::uint64_t unidirectional_session_id_ = 0;
  std::uint64_t datagram_session_id_ = 0;
  std::uint64_t reset_session_id_ = 0;
  std::uint32_t reset_application_error_ = 0;
  bool connected_ = false;
  bool datagram_send_enabled_ = false;
  bool reliable_reset_negotiated_ = false;
  bool peer_settings_ready_ = false;
  bool headers_seen_ = false;
  bool session_accepted_ = false;
  bool bidirectional_ready_ = false;
  bool unidirectional_ready_ = false;
  bool datagram_ready_ = false;
  bool reset_ready_ = false;
  bool closed_ = false;
};

struct listener_context {
  const QUIC_API_TABLE *api = nullptr;
  HQUIC configuration = nullptr;
  ntl::net::http3::quic_backend_sink *sink = nullptr;
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
#ifdef QUIC_API_ENABLE_PREVIEW_FEATURES
    settings.ReliableResetEnabled = TRUE;
    settings.IsSet.ReliableResetEnabled = TRUE;
#endif
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
  webtransport_sink server_sink(true);
  webtransport_sink client_sink(false);

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
  server_sink.attach(*server);
  client_sink.attach(*client);

  require(server_sink.wait_transport(10s),
          "server WebTransport QUIC parameters were not negotiated");
  require(client_sink.wait_transport(10s),
          "client WebTransport QUIC parameters were not negotiated");
  require(server_sink.session().send_local_settings(true).is_ok(),
          "server could not send HTTP/3 WebTransport SETTINGS");
  require(client_sink.session().send_local_settings(false).is_ok(),
          "client could not send HTTP/3 WebTransport SETTINGS");
  require(server_sink.wait_peer_settings(10s),
          "server did not receive client SETTINGS");
  require(client_sink.wait_peer_settings(10s),
          "client did not receive server SETTINGS");

  auto &client_session = client_sink.session();
  require(client_session.open_client(
              {.authority = "localhost",
               .path = "/webtransport",
               .origin = "https://localhost"})
              .is_ok(),
          "client could not send Extended CONNECT");
  require(client_sink.wait_session_accepted(10s),
          "client did not receive the WebTransport 200 response");

  const auto payload = make_bytes("webtransport-payload");
  require(client_session.send_bidirectional(payload).is_ok(),
          "client could not send WebTransport bidi data");
  require(client_session.send_unidirectional(payload).is_ok(),
          "client could not send WebTransport uni data");
  require(client_session.send_datagram(payload).is_ok(),
          "client could not send WebTransport datagram");
  require(server_sink.wait_payloads(payload, client_session.session_id(),
                                    10s),
          "server did not receive the negotiated WebTransport payloads");
  constexpr std::uint32_t reset_error = 0x10203040;
  auto reset_stream = client_session.open_bidirectional_stream();
  require(static_cast<bool>(reset_stream),
          "client could not open a resettable WebTransport stream");
  require(client_session.write(*reset_stream, payload).is_ok(),
          "client could not write the resettable WebTransport stream");
  require(client_session.reset(*reset_stream, reset_error).is_ok(),
          "client could not issue a reliable WebTransport reset");
  require(server_sink.wait_reset(client_session.session_id(), reset_error,
                                 10s),
          "server did not receive the mapped reliable WebTransport reset");
  require(client_session.finish().is_ok(),
          "client could not finish the WebTransport CONNECT stream");

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
      << "raw-msquic-loopback: tls13 settings extended-connect "
         "webtransport-bidi webtransport-uni h3-datagram "
         "reliable-reset-at application-error-map PASS\n";
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
