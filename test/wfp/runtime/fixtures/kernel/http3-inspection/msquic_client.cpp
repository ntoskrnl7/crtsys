#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "msquic_client.hpp"

#include <msquic.h>

// This translation unit is intentionally fixture-only: it generates the
// controlled HTTP/3 and WebTransport traffic used by runtime acceptance.

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ntl/net/http3/backend>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/standard_content_decoders>

namespace crtsys::wfp_kernel_http3 {
namespace {

using backend_connection = ntl::net::http3::msquic_backend::connection;
using namespace std::chrono_literals;

void require_quic(QUIC_STATUS status, const char *operation) {
  if (QUIC_FAILED(status))
    throw std::runtime_error(operation);
}

class loaded_msquic {
public:
  loaded_msquic() {
    module_ = ::LoadLibraryExW(
        L"msquic.dll", nullptr,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_)
      throw std::runtime_error(
          "msquic.dll is not beside the app or in System32");
    open_ = reinterpret_cast<MsQuicOpenVersionFn>(
        ::GetProcAddress(module_, "MsQuicOpenVersion"));
    close_ = reinterpret_cast<MsQuicCloseFn>(
        ::GetProcAddress(module_, "MsQuicClose"));
    if (!open_ || !close_)
      throw std::runtime_error("msquic.dll exports are incompatible");
    const void *table = nullptr;
    require_quic(open_(QUIC_API_VERSION_2, &table),
                 "MsQuicOpenVersion failed");
    api_ = static_cast<const QUIC_API_TABLE *>(table);
  }
  loaded_msquic(const loaded_msquic &) = delete;
  loaded_msquic &operator=(const loaded_msquic &) = delete;
  ~loaded_msquic() {
    if (api_)
      close_(api_);
    if (module_)
      (void)::FreeLibrary(module_);
  }
  const QUIC_API_TABLE *api() const noexcept { return api_; }

private:
  HMODULE module_ = nullptr;
  MsQuicOpenVersionFn open_ = nullptr;
  MsQuicCloseFn close_ = nullptr;
  const QUIC_API_TABLE *api_ = nullptr;
};

class registration_owner {
public:
  registration_owner(const QUIC_API_TABLE *api) : api_(api) {
    const QUIC_REGISTRATION_CONFIG configuration{
        "crtsys-kernel-http3-client", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    require_quic(api_->RegistrationOpen(&configuration, &handle_),
                 "RegistrationOpen failed");
  }
  ~registration_owner() {
    if (handle_)
      api_->RegistrationClose(handle_);
  }
  HQUIC get() const noexcept { return handle_; }

private:
  const QUIC_API_TABLE *api_;
  HQUIC handle_ = nullptr;
};

class configuration_owner {
public:
  configuration_owner(const QUIC_API_TABLE *api, HQUIC registration)
      : api_(api) {
    QUIC_BUFFER alpn{2, reinterpret_cast<std::uint8_t *>(
                           (const_cast<char *>("h3")))};
    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount = 16;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 8;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.DatagramReceiveEnabled = TRUE;
    settings.IsSet.DatagramReceiveEnabled = TRUE;
#ifdef QUIC_API_ENABLE_PREVIEW_FEATURES
    settings.ReliableResetEnabled = TRUE;
    settings.IsSet.ReliableResetEnabled = TRUE;
#endif
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    require_quic(api_->ConfigurationOpen(registration, &alpn, 1, &settings,
                                         sizeof(settings), nullptr, &handle_),
                 "ConfigurationOpen failed");
    QUIC_CREDENTIAL_CONFIG credentials{};
    credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    require_quic(api_->ConfigurationLoadCredential(handle_, &credentials),
                 "ConfigurationLoadCredential failed");
  }
  ~configuration_owner() {
    if (handle_)
      api_->ConfigurationClose(handle_);
  }
  HQUIC get() const noexcept { return handle_; }

private:
  const QUIC_API_TABLE *api_;
  HQUIC handle_ = nullptr;
};

class client_sink final : public ntl::net::quic::backend_sink,
                          public ntl::net::http3::inspection_sink {
public:
  client_sink() noexcept
      : qpack_(),
        inspector_(qpack_,
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

  ntl::status
  on_qpack_encoder_stream(ntl::net::scatter_view bytes) noexcept override {
    return bytes ? ntl::status{STATUS_NOT_SUPPORTED} : ntl::status::ok();
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    try {
      std::vector<std::byte> copied(bytes.size());
      if (!copied.empty() && !bytes.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      std::lock_guard guard(lock_);
      auto &wire = peer_unidirectional_[stream_id];
      if (copied.size() > 4096 - wire.size())
        return STATUS_BUFFER_OVERFLOW;
      wire.insert(wire.end(), copied.begin(), copied.end());
      const auto view = ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire));
      const auto type = ntl::net::http3::read_quic_varint(view);
      if (!type)
        return !final && type.status() == STATUS_BUFFER_TOO_SMALL
                   ? ntl::status::ok()
                   : type.status();
      if (type->value ==
          ntl::net::http3::webtransport::control_stream_type) {
        const auto settings =
            ntl::net::http3::webtransport::parse_control_stream(view);
        if (!settings) {
          if (!final && (settings.status() == STATUS_BUFFER_TOO_SMALL ||
                         settings.status() == STATUS_END_OF_FILE))
            return ntl::status::ok();
          return settings.status();
        }
        peer_settings_ = *settings;
        peer_settings_ready_ = true;
        peer_unidirectional_.erase(stream_id);
      } else if (type->value ==
                 ntl::net::http3::msquic_backend::
                     qpack_decoder_stream_type) {
        if (wire.size() >= type->encoded_size + 2) {
          qpack_acknowledged_ = true;
          result_.dynamic_qpack_acknowledged = true;
        }
      } else if (final) {
        return STATUS_NOT_SUPPORTED;
      }
      changed_.notify_all();
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  void on_closed(NTSTATUS status) noexcept override {
    {
      std::lock_guard guard(lock_);
      closed_ = true;
      close_status_ = status;
    }
    changed_.notify_all();
  }

  ntl::status on_headers(
      std::uint64_t,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    std::lock_guard guard(lock_);
    for (const auto &field : fields) {
      if (field.name == ":status") {
        if (field.value == "200")
          result_.status = 200;
        else if (field.value == "403")
          result_.status = 403;
        else
          return STATUS_DATA_ERROR;
      } else if (field.name == "content-encoding")
        result_.content_encoding = field.value;
    }
    return result_.status ? ntl::status::ok()
                          : ntl::status{STATUS_DATA_ERROR};
  }

  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view data) noexcept override {
    if (data.size() > 64 * 1024)
      return STATUS_BUFFER_OVERFLOW;
    std::vector<std::byte> copied(data.size());
    if (!copied.empty() && !data.copy_to(copied).is_ok())
      return STATUS_DATA_ERROR;
    try {
      std::lock_guard guard(lock_);
      if (copied.size() > 64 * 1024 - result_.body.size())
        return STATUS_BUFFER_OVERFLOW;
      result_.body.insert(result_.body.end(), copied.begin(), copied.end());
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_stream_end(std::uint64_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      complete_ = true;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  void wait_connected() {
    if (!wait_connected_for(10s))
      throw std::runtime_error("kernel HTTP/3 handshake timed out");
  }

  bool wait_connected_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(lock_);
    return changed_.wait_for(
               lock, timeout,
               [this] { return connected_ || closed_; }) &&
           connected_;
  }

  void send_request(std::string_view path, bool block,
                    bool dynamic_qpack) {
    if (!connection_)
      throw std::logic_error("HTTP/3 client backend is not attached");
    std::uint64_t control = 0;
    if (!connection_->open_unidirectional_stream(control).is_ok())
      throw std::runtime_error("cannot open HTTP/3 control stream");
    auto settings =
        ntl::net::http3::webtransport::encode_control_stream(false);
    if (!settings)
      throw std::runtime_error("cannot encode HTTP/3 SETTINGS");
    if (!connection_->write_stream(
                          control,
                          ntl::net::scatter_view::from_contiguous(*settings),
                          false)
             .is_ok())
      throw std::runtime_error("cannot send HTTP/3 SETTINGS");

    std::vector<ntl::net::http3::header_field> fields{
        {":method", "GET"},
        {":scheme", "https"},
        {":authority", "localhost"},
        {":path", std::string(path)},
    };
    if (block)
      fields.push_back({"x-ntl-block", "1"});
    ntl::net::http3::bounded_static_qpack_encoder encoder;
    auto headers = encoder.encode(fields, 16 * 1024);
    if (!headers)
      throw std::runtime_error("cannot encode HTTP/3 request headers");
    if (dynamic_qpack) {
      if (headers->size() < 2)
        throw std::runtime_error("invalid static QPACK prefix");
      (*headers)[0] = std::byte{0x02};
      (*headers)[1] = std::byte{0x00};
      headers->push_back(std::byte{0x80});
    }
    std::vector<std::byte> wire;
    if (!ntl::net::http3::append_quic_varint(
             wire, static_cast<std::uint64_t>(
                       ntl::net::http3::frame_type::headers))
             .is_ok() ||
        !ntl::net::http3::append_quic_varint(wire, headers->size()).is_ok())
      throw std::runtime_error("cannot frame HTTP/3 request headers");
    wire.insert(wire.end(), headers->begin(), headers->end());
    std::uint64_t request = 0;
    if (!connection_->open_request_stream(request).is_ok() ||
        !connection_->write_stream(
                         request,
                         ntl::net::scatter_view::from_contiguous(wire), true)
             .is_ok())
      throw std::runtime_error("cannot send HTTP/3 request");
    if (dynamic_qpack) {
      std::uint64_t qpack_encoder = 0;
      if (!connection_->open_unidirectional_stream(qpack_encoder).is_ok())
        throw std::runtime_error("cannot open QPACK encoder stream");
      constexpr std::array<std::byte, 7> instructions{
          std::byte{0x02}, std::byte{0x3f}, std::byte{0x21},
          std::byte{0x41}, std::byte{0x78}, std::byte{0x01},
          std::byte{0x79}};
      if (!connection_->write_stream(
                           qpack_encoder,
                           ntl::net::scatter_view::from_contiguous(
                               instructions),
                           false)
               .is_ok())
        throw std::runtime_error("cannot send QPACK encoder instructions");
      expect_qpack_ack_ = true;
    }
  }

  response wait_response() {
    std::unique_lock lock(lock_);
    if (!changed_.wait_for(lock, 10s, [this] {
          return (complete_ &&
                  (!expect_qpack_ack_ || qpack_acknowledged_)) ||
                 closed_;
        }) ||
        !complete_ || (expect_qpack_ack_ && !qpack_acknowledged_))
      throw std::runtime_error("kernel HTTP/3 response timed out");
    return result_;
  }

private:
  backend_connection *connection_ = nullptr;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>>
      peer_unidirectional_;
  ntl::net::http3::webtransport::peer_settings peer_settings_{};
  std::mutex lock_;
  std::condition_variable changed_;
  response result_{};
  NTSTATUS close_status_ = STATUS_SUCCESS;
  bool connected_ = false;
  bool peer_settings_ready_ = false;
  bool expect_qpack_ack_ = false;
  bool qpack_acknowledged_ = false;
  bool complete_ = false;
  bool closed_ = false;
};

class webtransport_client_sink final
    : public ntl::net::quic::backend_sink,
      public ntl::net::http3::inspection_sink {
public:
  explicit webtransport_client_sink(bool request_block = false) noexcept
      : request_block_(request_block), inspector_(qpack_,
                   {.maximum_concurrent_request_streams = 4,
                    .maximum_buffered_bytes_per_stream = 64 * 1024,
                    .frames = {64 * 1024}},
                   32 * 1024) {}

  void attach(const std::shared_ptr<backend_connection> &connection) {
    connection_ = connection.get();
    session_ = std::make_unique<
        ntl::net::http3::webtransport::backend_session>(
        connection,
        ntl::net::http3::webtransport::session_limits{
            .maximum_bidirectional_streams = 8,
            .maximum_unidirectional_streams = 8,
            .maximum_stream_data = 64 * 1024,
            .maximum_datagram_payload = 4096,
            .maximum_datagrams = 32});
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

  ntl::status on_peer_bidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    return consume_extension(stream_id, bytes, final,
                             ntl::net::http3::webtransport::
                                 stream_direction::bidirectional);
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    try {
      std::vector<std::byte> copied(bytes.size());
      if (!copied.empty() && !bytes.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      {
        std::lock_guard guard(lock_);
        auto &wire = unidirectional_[stream_id];
        if (copied.size() > 4096 - wire.size())
          return STATUS_BUFFER_OVERFLOW;
        wire.insert(wire.end(), copied.begin(), copied.end());
      }
      std::vector<std::byte> snapshot;
      {
        std::lock_guard guard(lock_);
        snapshot = unidirectional_[stream_id];
      }
      const auto view = ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(snapshot));
      const auto type = ntl::net::http3::read_quic_varint(view);
      if (!type)
        return !final && type.status() == STATUS_BUFFER_TOO_SMALL
                   ? ntl::status::ok()
                   : type.status();
      if (type->value ==
          ntl::net::http3::webtransport::control_stream_type) {
        const auto settings =
            ntl::net::http3::webtransport::parse_control_stream(view);
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
          unidirectional_.erase(stream_id);
        }
        changed_.notify_all();
        return ntl::status::ok();
      }
      return consume_extension(
          stream_id, ntl::net::scatter_view{}, final,
          ntl::net::http3::webtransport::stream_direction::unidirectional);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  ntl::status on_datagram(ntl::net::scatter_view bytes) noexcept override {
    const auto parsed = ntl::net::http::http3_datagram_view::parse(bytes);
    if (!parsed)
      return parsed.status();
    std::vector<std::byte> payload(parsed->payload().size());
    if (!payload.empty() && !parsed->payload().copy_to(payload).is_ok())
      return STATUS_DATA_ERROR;
    {
      std::lock_guard guard(lock_);
      datagram_echo_ = std::move(payload);
      datagram_session_ = parsed->request_stream_id();
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  void on_datagram_send_state(bool enabled, std::size_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      datagram_ready_ = enabled;
    }
    changed_.notify_all();
  }

  void on_reliable_reset_negotiated(bool enabled) noexcept override {
    {
      std::lock_guard guard(lock_);
      reliable_reset_ready_ = enabled;
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

  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    unsigned status = 0;
    for (const auto &field : fields) {
      if (field.name != ":status")
        continue;
      if (field.value == "200")
        status = 200;
      else if (field.value == "403")
        status = 403;
      else
        return STATUS_DATA_ERROR;
    }
    if (status == 0)
      return STATUS_DATA_ERROR;
    if (!session_)
      return STATUS_INVALID_DEVICE_STATE;
    const ntl::status lifecycle =
        status == 200
            ? session_->accept_client_response(stream_id, status)
            : session_->reject_client_response(stream_id, status);
    if (!lifecycle.is_ok())
      return lifecycle;
    {
      std::lock_guard guard(lock_);
      response_status_ = status;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }

  ntl::status on_stream_end(std::uint64_t) noexcept override {
    return ntl::status::ok();
  }

  webtransport_result run() {
    if (!connection_ || !session_)
      throw std::logic_error("WebTransport client is not attached");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s, [this] {
            return (connected_ && datagram_ready_ &&
                    reliable_reset_ready_) ||
                   closed_;
          }) ||
          !connected_ || !datagram_ready_ || !reliable_reset_ready_)
        throw std::runtime_error("WebTransport transport negotiation timed out");
    }
    session_->set_negotiated_transport({true, true});
    if (!session_->send_local_settings(false).is_ok())
      throw std::runtime_error("cannot send WebTransport SETTINGS");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s, [this] {
            return peer_settings_ready_ || closed_;
          }) ||
          !peer_settings_ready_ || !peer_settings_.server_ready())
        throw std::runtime_error("server WebTransport SETTINGS timed out");
    }
    ntl::net::http3::webtransport::connect_parameters request{
        .authority = "localhost",
        .path = "/webtransport",
        .origin = "https://localhost"};
    if (request_block_)
      request.additional_headers.push_back({"x-ntl-block", "1"});
    if (!session_->open_client(request).is_ok())
      throw std::runtime_error("cannot send WebTransport Extended CONNECT");
    if (session_->active() || !session_->client_response_pending())
      throw std::runtime_error(
          "WebTransport client became active before the response");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s,
                             [this] {
                               return response_status_ != 0 || closed_;
                             }) ||
          response_status_ == 0)
        throw std::runtime_error("WebTransport Extended CONNECT timed out");
    }
    if (request_block_) {
      if (response_status_ != 403 || session_->active() ||
          session_->client_response_pending() ||
          session_->send_datagram({}) != STATUS_INVALID_DEVICE_STATE)
        throw std::runtime_error(
            "blocked WebTransport Extended CONNECT established a session");
      return {.blocked = true};
    }
    if (response_status_ != 200 || !session_->active() ||
        session_->client_response_pending())
      throw std::runtime_error("WebTransport Extended CONNECT was rejected");
    constexpr std::string_view input = "client-payload";
    constexpr std::string_view expected = "ntl-inspected-payload";
    const auto payload = std::as_bytes(std::span(input));
    if (!session_->send_bidirectional(payload).is_ok() ||
        !session_->send_unidirectional(payload).is_ok() ||
        !session_->send_datagram(payload).is_ok() ||
        !session_->send_capsule(
                     ntl::net::http3::webtransport::wt_drain_session, {})
             .is_ok())
      throw std::runtime_error("cannot send WebTransport payloads");
    auto reset_stream = session_->open_bidirectional_stream();
    if (!reset_stream ||
        !session_->write(*reset_stream, payload, false).is_ok() ||
        !session_->reset(*reset_stream, 0x10203040).is_ok())
      throw std::runtime_error("cannot send reliable WebTransport reset");
    {
      std::unique_lock lock(lock_);
      const std::vector<std::byte> expected_bytes(
          reinterpret_cast<const std::byte *>(expected.data()),
          reinterpret_cast<const std::byte *>(expected.data() +
                                                expected.size()));
      if (!changed_.wait_for(lock, 10s, [this] {
            return (!bidirectional_echo_.empty() &&
                    !unidirectional_echo_.empty() &&
                    !datagram_echo_.empty()) ||
                   closed_;
          }) ||
          bidirectional_echo_ != expected_bytes ||
          unidirectional_echo_ != expected_bytes ||
          datagram_echo_ != expected_bytes ||
          bidirectional_session_ != session_->session_id() ||
          unidirectional_session_ != session_->session_id() ||
          datagram_session_ != session_->session_id())
        throw std::runtime_error("WebTransport transformed echoes timed out");
    }
    if (!session_->finish().is_ok())
      throw std::runtime_error("cannot finish WebTransport CONNECT stream");
    return {.connected = true,
            .settings = true,
            .extended_connect = true,
            .bidirectional = true,
            .unidirectional = true,
            .datagram = true,
            .capsule = true,
            .reliable_reset = true};
  }

private:
  ntl::status consume_extension(
      std::uint64_t stream_id, ntl::net::scatter_view bytes, bool final,
      ntl::net::http3::webtransport::stream_direction direction) noexcept {
    try {
      std::vector<std::byte> snapshot;
      {
        std::lock_guard guard(lock_);
        auto &map =
            direction == ntl::net::http3::webtransport::stream_direction::
                             bidirectional
                ? bidirectional_
                : unidirectional_;
        auto &wire = map[stream_id];
        if (bytes) {
          std::vector<std::byte> copied(bytes.size());
          if (!copied.empty() && !bytes.copy_to(copied).is_ok())
            return STATUS_DATA_ERROR;
          if (copied.size() > 4096 - wire.size())
            return STATUS_BUFFER_OVERFLOW;
          wire.insert(wire.end(), copied.begin(), copied.end());
        }
        snapshot = wire;
      }
      if (!final)
        return ntl::status::ok();
      const auto parsed = ntl::net::http3::webtransport::parse_stream_prefix(
          direction, ntl::net::scatter_view::from_contiguous(
                         std::span<const std::byte>(snapshot)));
      if (!parsed)
        return parsed.status();
      std::vector<std::byte> payload(parsed->body.size());
      if (!payload.empty() && !parsed->body.copy_to(payload).is_ok())
        return STATUS_DATA_ERROR;
      {
        std::lock_guard guard(lock_);
        auto &map =
            direction == ntl::net::http3::webtransport::stream_direction::
                             bidirectional
                ? bidirectional_
                : unidirectional_;
        map.erase(stream_id);
        if (direction == ntl::net::http3::webtransport::stream_direction::
                             bidirectional) {
          bidirectional_echo_ = std::move(payload);
          bidirectional_session_ = parsed->session_id;
        } else {
          unidirectional_echo_ = std::move(payload);
          unidirectional_session_ = parsed->session_id;
        }
      }
      changed_.notify_all();
      return ntl::status::ok();
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  }

  backend_connection *connection_ = nullptr;
  bool request_block_ = false;
  std::unique_ptr<ntl::net::http3::webtransport::backend_session> session_;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::mutex lock_;
  std::condition_variable changed_;
  ntl::net::http3::webtransport::peer_settings peer_settings_{};
  std::unordered_map<std::uint64_t, std::vector<std::byte>> bidirectional_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>> unidirectional_;
  std::vector<std::byte> bidirectional_echo_;
  std::vector<std::byte> unidirectional_echo_;
  std::vector<std::byte> datagram_echo_;
  std::uint64_t bidirectional_session_ = 0;
  std::uint64_t unidirectional_session_ = 0;
  std::uint64_t datagram_session_ = 0;
  bool connected_ = false;
  bool datagram_ready_ = false;
  bool reliable_reset_ready_ = false;
  bool peer_settings_ready_ = false;
  unsigned response_status_ = 0;
  bool closed_ = false;
};

} // namespace

response exchange_http3(int address_family, std::uint16_t port,
                        std::string_view path, bool block,
                        bool dynamic_qpack) {
  loaded_msquic library;
  registration_owner registration(library.api());
  configuration_owner configuration(library.api(), registration.get());
  client_sink sink;
  const QUIC_ADDRESS_FAMILY family =
      address_family == AF_INET
          ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET)
          : address_family == AF_INET6
                ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET6)
                : static_cast<QUIC_ADDRESS_FAMILY>(
                      QUIC_ADDRESS_FAMILY_UNSPEC);
  auto connected = backend_connection::try_connect_borrowed(
      library.api(), registration.get(), configuration.get(), "localhost",
      port, sink, family,
      {.maximum_streams = 32,
       .maximum_receive_indication = 128 * 1024,
       .maximum_send_size = 128 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error("cannot create HTTP/3 client connection");
  auto connection = std::move(*connected);
  sink.attach(*connection);
  sink.wait_connected();
  sink.send_request(path, block, dynamic_qpack);
  auto result = sink.wait_response();
  connection->stop();
  if (!connection->drain().is_ok())
    throw std::runtime_error("HTTP/3 client shutdown timed out");
  if (!result.content_encoding.empty()) {
    ntl::net::inspection::content_decoder_registry decoders;
    ntl::net::inspection::register_standard_content_decoders(decoders);
    auto decoded = ntl::net::inspection::decode_content_encoding(
        decoders,
        ntl::net::scatter_view::from_contiguous(
            std::span<const std::byte>(result.body)),
        result.content_encoding,
        {.maximum_encoded_size = 64 * 1024,
         .maximum_decoded_size = 64 * 1024,
         .maximum_expansion_ratio = 64,
         .maximum_coding_layers = 1});
    if (!decoded)
      throw std::runtime_error("cannot decode kernel HTTP/3 response");
    result.body = std::move(*decoded);
  }
  return result;
}

webtransport_result exercise_webtransport_impl(int address_family,
                                               std::uint16_t port,
                                               bool request_block) {
  loaded_msquic library;
  registration_owner registration(library.api());
  configuration_owner configuration(library.api(), registration.get());
  webtransport_client_sink sink(request_block);
  const QUIC_ADDRESS_FAMILY family =
      address_family == AF_INET
          ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET)
          : address_family == AF_INET6
                ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET6)
                : static_cast<QUIC_ADDRESS_FAMILY>(
                      QUIC_ADDRESS_FAMILY_UNSPEC);
  auto connected = backend_connection::try_connect_borrowed(
      library.api(), registration.get(), configuration.get(), "localhost",
      port, sink, family,
      {.maximum_streams = 64,
       .maximum_receive_indication = 128 * 1024,
       .maximum_send_size = 128 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error("cannot create WebTransport client connection");
  std::shared_ptr<backend_connection> connection = std::move(*connected);
  sink.attach(connection);
  auto result = sink.run();
  connection->stop();
  if (!connection->drain().is_ok())
    throw std::runtime_error("WebTransport client shutdown timed out");
  return result;
}

webtransport_result exercise_webtransport(int address_family,
                                          std::uint16_t port) {
  return exercise_webtransport_impl(address_family, port, false);
}

bool exercise_blocked_webtransport(int address_family,
                                   std::uint16_t port) {
  return exercise_webtransport_impl(address_family, port, true).blocked;
}

bool http3_connect_is_blocked(int address_family, std::uint16_t port,
                              std::uint32_t timeout_milliseconds) {
  loaded_msquic library;
  registration_owner registration(library.api());
  configuration_owner configuration(library.api(), registration.get());
  client_sink sink;
  const QUIC_ADDRESS_FAMILY family =
      address_family == AF_INET
          ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET)
          : address_family == AF_INET6
                ? static_cast<QUIC_ADDRESS_FAMILY>(QUIC_ADDRESS_FAMILY_INET6)
                : throw std::invalid_argument(
                      "unsupported HTTP/3 address family");
  auto connected = backend_connection::try_connect_borrowed(
      library.api(), registration.get(), configuration.get(), "localhost",
      port, sink, family,
      {.maximum_streams = 4,
       .maximum_receive_indication = 16 * 1024,
       .maximum_send_size = 16 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 5s});
  if (!connected)
    throw std::runtime_error("cannot create negative HTTP/3 client");
  auto connection = std::move(*connected);
  sink.attach(*connection);
  const bool reached_server = sink.wait_connected_for(
      std::chrono::milliseconds(timeout_milliseconds));
  connection->stop();
  if (!connection->drain().is_ok())
    throw std::runtime_error("negative HTTP/3 client shutdown timed out");
  return !reached_server;
}

} // namespace crtsys::wfp_kernel_http3
