#include <msquic.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

#include "test_certificate.hpp"
#include "http3_inspection_policy.hpp"

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

void append_http3_frame(std::vector<std::byte> &wire,
                        ntl::net::http3::frame_type type,
                        std::span<const std::byte> payload) {
  require(ntl::net::http3::append_quic_varint(
              wire, static_cast<std::uint64_t>(type))
              .is_ok(),
          "cannot encode HTTP/3 frame type");
  require(ntl::net::http3::append_quic_varint(wire, payload.size()).is_ok(),
          "cannot encode HTTP/3 frame length");
  wire.insert(wire.end(), payload.begin(), payload.end());
}

class recording_sink final
    : public ntl::net::quic::backend_sink {
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
    : public ntl::net::quic::backend_sink {
public:
  explicit webtransport_sink(bool server) : server_(server) {
    crtsys::examples::wfp::http3_inspection::
        configure_webtransport_policy(transform_);
  }

  void attach(
      std::shared_ptr<ntl::net::quic::transport_backend> connection) {
    std::lock_guard guard(lock_);
    session_ = std::make_unique<
        ntl::net::http3::webtransport::backend_session>(
            std::move(connection));
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
    bool consume_capsules = false;
    try {
      std::lock_guard guard(lock_);
      auto &wire = request_wires_[stream_id];
      if (!append(bytes, wire))
        return STATUS_INSUFFICIENT_RESOURCES;
      buffered = wire;
      if (headers_seen_) {
        if (stream_id != connect_stream_id_)
          return STATUS_DATA_ERROR;
        consume_capsules = true;
      }
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (consume_capsules)
      return consume_connect_capsules(stream_id, buffered, final);
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
      const bool blocked =
          crtsys::examples::wfp::http3_inspection::block_requested(
              std::span<const ntl::net::http3::header_field>(
                  headers->fields));
      ntl::net::http3::webtransport::backend_session *session = nullptr;
      {
        std::lock_guard guard(lock_);
        if (!session_)
          return STATUS_INVALID_DEVICE_STATE;
        session = session_.get();
        request_wires_.erase(stream_id);
        if (!blocked &&
            (!peer_settings_ready_ || !peer_settings_.client_ready() ||
                   !datagram_send_enabled_ ||
                   !reliable_reset_negotiated_))
          return STATUS_NOT_SUPPORTED;
        if (!blocked) {
          headers_seen_ = true;
          connect_stream_id_ = stream_id;
          session->set_negotiated_transport({true, true});
        }
      }
      const ntl::status decision =
          blocked ? session->reject_server(stream_id, 403)
                  : session->accept_server(stream_id);
      if (!decision.is_ok())
        return decision;
      if (blocked) {
        std::lock_guard guard(lock_);
        session_rejected_ = true;
        rejection_status_ = 403;
      }
    } else {
      std::string_view response_status;
      for (const auto &field : headers->fields) {
        if (field.name == ":status") {
          if (!response_status.empty())
            return STATUS_DATA_ERROR;
          response_status = field.value;
          continue;
        }
        // A terminal HTTP response may carry ordinary representation
        // headers (for example content-type and content-length).  Unknown
        // pseudo-fields are invalid, but ordinary fields do not change the
        // WebTransport CONNECT decision.
        if (!field.name.empty() && field.name.front() == ':')
          return STATUS_DATA_ERROR;
      }
      const bool accepted = response_status == "200";
      const bool rejected = response_status == "403";
      if (!accepted && !rejected)
        return STATUS_DATA_ERROR;
      ntl::net::http3::webtransport::backend_session *session = nullptr;
      {
        std::lock_guard guard(lock_);
        if (!session_)
          return STATUS_INVALID_DEVICE_STATE;
        session = session_.get();
        request_wires_.erase(stream_id);
      }
      const ntl::status decision =
          accepted ? session->accept_client_response(stream_id, 200)
                   : session->reject_client_response(stream_id, 403);
      if (!decision.is_ok())
        return decision;
      {
        std::lock_guard guard(lock_);
        headers_seen_ = accepted;
        session_accepted_ = accepted;
        session_rejected_ = rejected;
        rejection_status_ = rejected ? 403u : 0u;
        if (accepted)
          connect_stream_id_ = stream_id;
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

  bool wait_session_rejected(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout, [this] {
      return session_rejected_ || closed_;
    }) && session_rejected_ && rejection_status_ == 403;
  }

  bool wait_payloads(std::span<const std::byte> expected,
                     std::uint64_t session_id,
                     std::uint64_t capsule_type,
                     std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    const auto ready = changed_.wait_for(lock, timeout, [this] {
      return (bidirectional_ready_ && unidirectional_ready_ &&
              datagram_ready_ && capsule_ready_) ||
             closed_;
    });
    const std::vector<std::byte> expected_vector(expected.begin(),
                                                  expected.end());
    return ready && bidirectional_ready_ && unidirectional_ready_ &&
           datagram_ready_ && capsule_ready_ &&
           bidirectional_session_id_ == session_id &&
           unidirectional_session_id_ == session_id &&
           datagram_session_id_ == session_id &&
           capsule_session_id_ == session_id &&
           capsule_type_ == capsule_type &&
           bidirectional_payload_ == expected_vector &&
           unidirectional_payload_ == expected_vector &&
           datagram_payload_ == expected_vector &&
           capsule_payload_ == expected_vector;
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

  static bool capsule_reassembly_contract() noexcept {
    try {
    constexpr std::uint64_t stream_id = 0;
    constexpr std::uint64_t capsule_type = 0x190b4d4;
    const auto payload = make_bytes("split-capsule-payload");
    const auto capsule = ntl::net::http::encode_capsule(
        capsule_type, payload, {.maximum_payload_size = 64 * 1024});
    if (!capsule || capsule->size() < 2)
      return false;
    const std::size_t split = capsule->size() / 2;
    std::vector<std::byte> wire;
    append_http3_frame(
        wire, ntl::net::http3::frame_type::data,
        std::span<const std::byte>(*capsule).first(split));
    append_http3_frame(
        wire, ntl::net::http3::frame_type::data,
        std::span<const std::byte>(*capsule).subspan(split));

    webtransport_sink complete(true);
    {
      std::lock_guard guard(complete.lock_);
      complete.headers_seen_ = true;
      complete.connect_stream_id_ = stream_id;
    }
    constexpr std::array<std::size_t, 5> chunks{1, 2, 3, 5, 7};
    std::size_t offset = 0;
    std::size_t chunk_index = 0;
    while (offset < wire.size()) {
      const std::size_t count = (std::min)(
          chunks[chunk_index++ % chunks.size()], wire.size() - offset);
      const auto bytes = std::span<const std::byte>(wire).subspan(
          offset, count);
      if (!complete.on_request_stream(
                       stream_id,
                       ntl::net::scatter_view::from_contiguous(bytes),
                       false)
               .is_ok())
        return false;
      offset += count;
    }
    if (!complete.on_request_stream(
                     stream_id, ntl::net::scatter_view{}, true)
             .is_ok())
      return false;
    {
      std::lock_guard guard(complete.lock_);
      if (!complete.capsule_ready_ ||
          complete.capsule_session_id_ != stream_id ||
          complete.capsule_type_ != capsule_type ||
          complete.capsule_payload_ != payload)
        return false;
    }
    {
      std::lock_guard guard(complete.capsule_parse_lock_);
      if (!complete.capsule_stream_wire_.empty())
        return false;
    }

    webtransport_sink truncated(true);
    {
      std::lock_guard guard(truncated.lock_);
      truncated.headers_seen_ = true;
      truncated.connect_stream_id_ = stream_id;
    }
    std::vector<std::byte> truncated_wire;
    append_http3_frame(
        truncated_wire, ntl::net::http3::frame_type::data,
        std::span<const std::byte>(*capsule).first(split));
    if (!truncated.on_request_stream(
                      stream_id,
                      ntl::net::scatter_view::from_contiguous(
                          std::span<const std::byte>(truncated_wire)),
                      false)
             .is_ok())
      return false;
    return truncated.on_request_stream(
                         stream_id, ntl::net::scatter_view{}, true) ==
           STATUS_END_OF_FILE;
    } catch (...) {
      return false;
    }
  }

private:
  ntl::status consume_connect_capsules(
      std::uint64_t stream_id,
      const std::vector<std::byte> &buffered,
      bool final) noexcept {
    const auto wire = as_scatter(buffered);
    ntl::net::http3::frame_framer frame_framer({64 * 1024});
    std::size_t consumed = 0;
    while (consumed < wire.size()) {
      auto remaining = wire.subview(consumed);
      if (!remaining)
        return remaining.status();
      const auto frame_probe = frame_framer.probe(*remaining);
      if (frame_probe.state() == ntl::net::framing::probe_state::need_more)
        break;
      if (frame_probe.state() !=
          ntl::net::framing::probe_state::complete)
        return frame_probe.error();
      auto frame_wire = remaining->subview(0, frame_probe.frame_size());
      if (!frame_wire)
        return frame_wire.status();
      const auto frame = ntl::net::http3::frame_view::parse(
          *frame_wire, {64 * 1024});
      if (!frame || frame->header().type() !=
                        ntl::net::http3::frame_type::data)
        return frame ? ntl::status{STATUS_DATA_ERROR} : frame.status();

      const ntl::status capsule_status =
          consume_capsule_bytes(stream_id, frame->payload());
      if (!capsule_status.is_ok())
        return capsule_status;
      consumed += frame_probe.frame_size();
    }
    try {
      std::lock_guard guard(lock_);
      auto found = request_wires_.find(stream_id);
      if (found == request_wires_.end() ||
          found->second.size() < consumed)
        return STATUS_DATA_ERROR;
      found->second.erase(found->second.begin(),
                          found->second.begin() + consumed);
      if (final) {
        if (!found->second.empty())
          return STATUS_END_OF_FILE;
        request_wires_.erase(found);
      }
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (final) {
      std::lock_guard guard(capsule_parse_lock_);
      if (!capsule_stream_wire_.empty())
        return STATUS_END_OF_FILE;
    }
    return ntl::status::ok();
  }

  ntl::status consume_capsule_bytes(
      std::uint64_t stream_id,
      ntl::net::scatter_view bytes) noexcept {
    constexpr std::size_t maximum_capsule_wire = 64 * 1024 + 16;
    std::lock_guard parse_guard(capsule_parse_lock_);
    if (capsule_stream_wire_.size() > maximum_capsule_wire ||
        bytes.size() > maximum_capsule_wire -
                           capsule_stream_wire_.size())
      return STATUS_BUFFER_OVERFLOW;
    if (!append(bytes, capsule_stream_wire_))
      return STATUS_INSUFFICIENT_RESOURCES;

    ntl::net::http::capsule_framer framer(
        {.maximum_payload_size = 64 * 1024});
    while (!capsule_stream_wire_.empty()) {
      const auto wire = as_scatter(capsule_stream_wire_);
      const auto probe = framer.probe(wire);
      if (probe.state() == ntl::net::framing::probe_state::need_more)
        return ntl::status::ok();
      if (probe.state() != ntl::net::framing::probe_state::complete)
        return probe.error();
      auto capsule_wire = wire.subview(0, probe.frame_size());
      if (!capsule_wire)
        return capsule_wire.status();
      const auto capsule = ntl::net::http::capsule_view::parse(
          *capsule_wire, {.maximum_payload_size = 64 * 1024});
      if (!capsule)
        return capsule.status();
      ntl::net::http3::webtransport::payload semantic{
          ntl::net::http3::webtransport::payload_kind::capsule,
          stream_id,
          ntl::net::http3::webtransport::stream_direction::bidirectional,
          capsule->header().type,
          std::vector<std::byte>(capsule->payload().size())};
      if (!semantic.bytes.empty() &&
          !capsule->payload().copy_to(semantic.bytes).is_ok())
        return STATUS_DATA_ERROR;
      {
        std::lock_guard transform_guard(transform_lock_);
        const auto transformed = transform_.apply(semantic);
        if (transformed.action !=
                ntl::net::http3::webtransport::transform_action::forward ||
            transformed.failure != STATUS_SUCCESS)
          return transformed.failure == STATUS_SUCCESS
                     ? ntl::status{STATUS_ACCESS_DENIED}
                     : ntl::status{transformed.failure};
      }
      {
        std::lock_guard guard(lock_);
        capsule_payload_ = std::move(semantic.bytes);
        capsule_session_id_ = stream_id;
        capsule_type_ = capsule->header().type;
        capsule_ready_ = true;
      }
      changed_.notify_all();
      capsule_stream_wire_.erase(
          capsule_stream_wire_.begin(),
          capsule_stream_wire_.begin() + probe.frame_size());
    }
    return ntl::status::ok();
  }

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
  std::mutex capsule_parse_lock_;
  std::condition_variable changed_;
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
  std::vector<std::byte> capsule_payload_;
  std::vector<std::byte> capsule_stream_wire_;
  std::uint64_t connect_stream_id_ = 0;
  std::uint64_t bidirectional_session_id_ = 0;
  std::uint64_t unidirectional_session_id_ = 0;
  std::uint64_t datagram_session_id_ = 0;
  std::uint64_t capsule_session_id_ = 0;
  std::uint64_t capsule_type_ = 0;
  std::uint64_t reset_session_id_ = 0;
  std::uint32_t reset_application_error_ = 0;
  bool connected_ = false;
  bool datagram_send_enabled_ = false;
  bool reliable_reset_negotiated_ = false;
  bool peer_settings_ready_ = false;
  bool headers_seen_ = false;
  bool session_accepted_ = false;
  bool session_rejected_ = false;
  unsigned rejection_status_ = 0;
  bool bidirectional_ready_ = false;
  bool unidirectional_ready_ = false;
  bool datagram_ready_ = false;
  bool capsule_ready_ = false;
  bool reset_ready_ = false;
  bool closed_ = false;
};

class ordinary_client_sink final
    : public ntl::net::quic::backend_sink,
      public ntl::net::http3::inspection_sink {
public:
  ordinary_client_sink() noexcept
      : inspector_(qpack_,
                   {.maximum_concurrent_request_streams = 8,
                    .maximum_buffered_bytes_per_stream = 64 * 1024,
                    .frames = {64 * 1024}},
                   32 * 1024) {}

  void attach(backend_connection &connection) noexcept {
    connection_ = &connection;
  }

  ntl::status on_connected(std::string_view alpn) noexcept override {
    {
      std::lock_guard guard(lock_);
      connected_ = alpn == "h3";
    }
    changed_.notify_all();
    return connected_ ? ntl::status::ok()
                      : ntl::status{STATUS_PROTOCOL_NOT_SUPPORTED};
  }

  ntl::status on_request_stream(std::uint64_t stream_id,
                                ntl::net::scatter_view bytes,
                                bool final) noexcept override {
    return inspector_.consume_request_stream(stream_id, bytes, final, *this);
  }

  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view bytes) noexcept override {
    return bytes ? ntl::status{STATUS_NOT_SUPPORTED} : ntl::status::ok();
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool) noexcept override {
    try {
      std::vector<std::byte> copied(bytes.size());
      if (!copied.empty() && !bytes.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      std::lock_guard guard(lock_);
      auto &wire = unidirectional_[stream_id];
      wire.insert(wire.end(), copied.begin(), copied.end());
      const auto view = as_scatter(wire);
      const auto type = ntl::net::http3::read_quic_varint(view);
      if (type && type->value ==
                      ntl::net::http3::msquic_backend::
                          qpack_decoder_stream_type &&
          wire.size() > type->encoded_size) {
        acknowledged_ = true;
        changed_.notify_all();
      }
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  void on_closed(NTSTATUS) noexcept override {
    {
      std::lock_guard guard(lock_);
      closed_ = true;
    }
    changed_.notify_all();
  }

  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    try {
      std::lock_guard guard(lock_);
      auto &response = responses_[stream_id];
      for (const auto &field : fields) {
        if (field.name == ":status") {
          if (field.value == "200")
            response.status = 200;
          else if (field.value == "403")
            response.status = 403;
        }
        else if (field.name == "content-encoding")
          response.encoding = field.value;
      }
      if ((response.status == 200 && !response.encoding.empty()) ||
          (response.status == 403 && response.encoding.empty()))
        return ntl::status::ok();
      return STATUS_DATA_ERROR;
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_data(std::uint64_t stream_id,
                      ntl::net::scatter_view data) noexcept override {
    try {
      std::vector<std::byte> copied(data.size());
      if (!copied.empty() && !data.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      std::lock_guard guard(lock_);
      auto &body = responses_[stream_id].body;
      if (copied.size() > 64 * 1024 - body.size())
        return STATUS_BUFFER_OVERFLOW;
      body.insert(body.end(), copied.begin(), copied.end());
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_stream_end(std::uint64_t stream_id) noexcept override {
    try {
      response_state response;
      {
        std::lock_guard guard(lock_);
        const auto found = responses_.find(stream_id);
        if (found == responses_.end())
          return STATUS_NOT_FOUND;
        response = std::move(found->second);
        responses_.erase(found);
      }
      if (response.status == 403) {
        constexpr std::string_view expected =
            crtsys::examples::wfp::http3_inspection::blocked_html;
        if (response.body != make_bytes(expected))
          return STATUS_DATA_ERROR;
      } else {
        ntl::net::inspection::content_decoder_registry decoders;
        ntl::net::inspection::register_standard_content_decoders(decoders);
        const auto decoded = ntl::net::inspection::decode_content_encoding(
            decoders, as_scatter(response.body), response.encoding,
            {.maximum_encoded_size = 64 * 1024,
             .maximum_decoded_size = 64 * 1024,
             .maximum_expansion_ratio = 64,
             .maximum_coding_layers = 1});
        constexpr std::string_view expected =
            crtsys::examples::wfp::http3_inspection::allowed_html;
        if (!decoded || *decoded != make_bytes(expected))
          return decoded ? ntl::status{STATUS_DATA_ERROR} : decoded.status();
      }
      {
        std::lock_guard guard(lock_);
        if (response.status == 403)
          ++blocked_;
        else
          encodings_.insert(response.encoding);
        ++completed_;
      }
      changed_.notify_all();
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  bool wait_connected(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(lock, timeout,
                             [this] { return connected_ || closed_; }) &&
           connected_;
  }

  void send_requests() {
    require(connection_ != nullptr, "ordinary client is not attached");
    std::uint64_t control = 0;
    auto settings = ntl::net::http3::webtransport::encode_control_stream(false);
    require(settings &&
                connection_->open_unidirectional_stream(control).is_ok() &&
                connection_->write_stream(control, as_scatter(*settings),
                                          false)
                    .is_ok(),
            "cannot send ordinary HTTP/3 SETTINGS");
    constexpr std::array<std::pair<std::string_view, bool>, 4> requests{{
        {"/gzip", false}, {"/deflate", false}, {"/br", false},
        {"/blocked", true}}};
    for (const auto &[path, blocked] : requests) {
      std::vector<ntl::net::http3::header_field> fields{
          {":method", "GET"},
          {":scheme", "https"},
          {":authority", "localhost"},
          {":path", std::string(path)}};
      if (blocked)
        fields.push_back({"x-ntl-block", "1"});
      ntl::net::http3::bounded_static_qpack_encoder encoder;
      auto headers = encoder.encode(fields, 16 * 1024);
      require(headers && headers->size() >= 2,
              "cannot encode dynamic QPACK request");
      (*headers)[0] = std::byte{0x02};
      (*headers)[1] = std::byte{0x00};
      headers->push_back(std::byte{0x80});
      std::vector<std::byte> wire;
      append_http3_frame(wire, ntl::net::http3::frame_type::headers,
                         *headers);
      std::uint64_t request = 0;
      require(connection_->open_request_stream(request).is_ok() &&
                  connection_->write_stream(request, as_scatter(wire), true)
                      .is_ok(),
              "cannot send dynamic HTTP/3 request");
    }
    std::uint64_t encoder_stream = 0;
    constexpr std::array<std::byte, 7> instructions{
        std::byte{0x02}, std::byte{0x3f}, std::byte{0x21},
        std::byte{0x41}, std::byte{0x78}, std::byte{0x01},
        std::byte{0x79}};
    require(connection_->open_unidirectional_stream(encoder_stream).is_ok() &&
                connection_->write_stream(
                                encoder_stream,
                                ntl::net::scatter_view::from_contiguous(
                                    instructions),
                                false)
                    .is_ok(),
            "cannot send dynamic QPACK encoder instructions");
  }

  bool wait_completed(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    const bool ready = changed_.wait_for(lock, timeout, [this] {
      return (completed_ == 4 && acknowledged_) || closed_;
    });
    const bool valid = ready && completed_ == 4 && blocked_ == 1 &&
                       acknowledged_ &&
                       encodings_.size() == 3 && encodings_.contains("gzip") &&
                       encodings_.contains("deflate") &&
                       encodings_.contains("br");
    if (!valid)
      std::cerr << "ordinary client state: completed=" << completed_
                << " qpack_ack=" << acknowledged_
                << " blocked=" << blocked_
                << " encodings=" << encodings_.size()
                << " closed=" << closed_ << '\n';
    return valid;
  }

private:
  struct response_state {
    unsigned status = 0;
    std::string encoding;
    std::vector<std::byte> body;
  };

  backend_connection *connection_ = nullptr;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::mutex lock_;
  std::condition_variable changed_;
  std::unordered_map<std::uint64_t, response_state> responses_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>> unidirectional_;
  std::unordered_set<std::string> encodings_;
  std::size_t completed_ = 0;
  std::size_t blocked_ = 0;
  bool connected_ = false;
  bool acknowledged_ = false;
  bool closed_ = false;
};

struct listener_context {
  const QUIC_API_TABLE *api = nullptr;
  HQUIC configuration = nullptr;
  std::function<std::shared_ptr<ntl::net::quic::backend_sink>(
      std::shared_ptr<ntl::net::quic::transport_backend>)> sink_factory;
  std::mutex lock;
  std::condition_variable changed;
  std::shared_ptr<backend_connection> accepted;
  ntl::status accept_status{STATUS_PENDING};
  std::uint64_t accepted_count = 0;
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
  if (!event->NEW_CONNECTION.Info)
    return QUIC_STATUS_INVALID_PARAMETER;

  auto indication = ntl::net::http3::msquic_backend::
      borrowed_accepted_connection::from_native(
          event->NEW_CONNECTION.Connection,
          *event->NEW_CONNECTION.Info);
  auto accepted = state->sink_factory
      ? backend_connection::try_accept_with_sink_borrowed(
            state->api, std::move(indication),
            state->configuration, state->sink_factory)
      : ntl::result<std::shared_ptr<backend_connection>>(
            ntl::unexpected(STATUS_INVALID_DEVICE_STATE));
  {
    std::lock_guard guard(state->lock);
    ++state->accepted_count;
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
    module_ = ::LoadLibraryExW(
        L"msquic.dll", nullptr,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(module_ != nullptr,
            "msquic.dll is not beside the app or in System32");
    open_ = reinterpret_cast<MsQuicOpenVersionFn>(
        ::GetProcAddress(module_, "MsQuicOpenVersion"));
    close_ = reinterpret_cast<MsQuicCloseFn>(
        ::GetProcAddress(module_, "MsQuicClose"));
    require(open_ != nullptr && close_ != nullptr,
            "msquic.dll exports are incompatible");
    const void *table = nullptr;
    require_quic(open_(QUIC_API_VERSION_2, &table),
                 "MsQuicOpenVersion failed");
    api_ = static_cast<const QUIC_API_TABLE *>(table);

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

    // VM automation launches the fixture through VMware Tools without an
    // interactive logon token.  Keep the acceptance identity in the machine
    // key store; the owning fixture deletes the unique container at teardown.
    // Product code still chooses user or machine scope explicitly.
    certificate_ =
        std::make_unique<crtsys::wfp_sample::ephemeral_certificate>(
            true);
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
      close_(api_);
    if (module_)
      (void)::FreeLibrary(module_);
  }

  const QUIC_API_TABLE *borrowed_native_api() const noexcept { return api_; }
  HQUIC borrowed_native_registration() const noexcept { return registration_; }
  HQUIC borrowed_native_server_configuration() const noexcept {
    return server_configuration_;
  }
  HQUIC borrowed_native_client_configuration() const noexcept {
    return client_configuration_;
  }
  const QUIC_BUFFER &borrowed_alpn() const noexcept { return alpn_; }

private:
  HMODULE module_ = nullptr;
  MsQuicOpenVersionFn open_ = nullptr;
  MsQuicCloseFn close_ = nullptr;
  const QUIC_API_TABLE *api_ = nullptr;
  HQUIC registration_ = nullptr;
  HQUIC server_configuration_ = nullptr;
  HQUIC client_configuration_ = nullptr;
  QUIC_BUFFER alpn_{};
  std::unique_ptr<crtsys::wfp_sample::ephemeral_certificate>
      certificate_;
};

int socket_family(QUIC_ADDRESS_FAMILY family) {
  if (family == QUIC_ADDRESS_FAMILY_INET)
    return AF_INET;
  if (family == QUIC_ADDRESS_FAMILY_INET6)
    return AF_INET6;
  throw std::invalid_argument("unsupported QUIC address family");
}

} // namespace
