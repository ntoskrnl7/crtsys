#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "managed_http3_origin.hpp"

#include <msquic.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ntl/net/http3/backend>
#include <ntl/net/grpc/framing>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/inspection/standard_content_encoders>

namespace crtsys::wfp_kernel_browser_https {
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
                 "MsQuicOpenVersion(origin) failed");
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
  explicit registration_owner(const QUIC_API_TABLE *api) : api_(api) {
    const QUIC_REGISTRATION_CONFIG configuration{
        "crtsys-kernel-browser-controlled-origin",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    require_quic(api_->RegistrationOpen(&configuration, &handle_),
                 "RegistrationOpen(origin) failed");
  }
  registration_owner(const registration_owner &) = delete;
  registration_owner &operator=(const registration_owner &) = delete;
  ~registration_owner() {
    if (handle_)
      api_->RegistrationClose(handle_);
  }
  HQUIC get() const noexcept { return handle_; }

private:
  const QUIC_API_TABLE *api_ = nullptr;
  HQUIC handle_ = nullptr;
};

class server_configuration {
public:
  server_configuration(
      const QUIC_API_TABLE *api, HQUIC registration,
      const std::array<
          std::byte,
          wfp_kernel_browser_https_inspection::certificate_thumbprint_size>
          &thumbprint)
      : api_(api) {
    QUIC_BUFFER alpn{2, reinterpret_cast<std::uint8_t *>(
                           (const_cast<char *>("h3")))};
    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount = 64;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 8;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    require_quic(api_->ConfigurationOpen(
                     registration, &alpn, 1, &settings, sizeof(settings),
                     nullptr, &handle_),
                 "ConfigurationOpen(origin) failed");
    QUIC_CERTIFICATE_HASH_STORE hash{};
    static_assert(sizeof(hash.ShaHash) ==
                  wfp_kernel_browser_https_inspection::
                      certificate_thumbprint_size);
    std::memcpy(hash.ShaHash, thumbprint.data(), thumbprint.size());
    std::memcpy(hash.StoreName, "MY", 2);
    hash.Flags = QUIC_CERTIFICATE_HASH_STORE_FLAG_MACHINE_STORE;
    QUIC_CREDENTIAL_CONFIG credentials{};
    credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
    credentials.CertificateHashStore = &hash;
    credentials.Flags = QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;
    require_quic(api_->ConfigurationLoadCredential(handle_, &credentials),
                 "ConfigurationLoadCredential(origin) failed");
  }
  server_configuration(const server_configuration &) = delete;
  server_configuration &operator=(const server_configuration &) = delete;
  ~server_configuration() {
    if (handle_)
      api_->ConfigurationClose(handle_);
  }
  HQUIC get() const noexcept { return handle_; }

private:
  const QUIC_API_TABLE *api_ = nullptr;
  HQUIC handle_ = nullptr;
};

std::vector<std::byte> bytes_of(std::string_view value) {
  return {reinterpret_cast<const std::byte *>(value.data()),
          reinterpret_cast<const std::byte *>(value.data() + value.size())};
}

bool grpc_payload_equals(std::span<const std::byte> wire,
                         std::string_view expected) noexcept {
  const auto header = ntl::net::grpc::inspect_header(
      ntl::net::scatter_view::from_contiguous(wire), 4096);
  if (!header || header->compressed || wire.size() != 5 + header->payload_size ||
      header->payload_size != expected.size())
    return false;
  return std::equal(
      wire.begin() + 5, wire.end(),
      reinterpret_cast<const std::byte *>(expected.data()));
}

void append_frame(std::vector<std::byte> &wire,
                  ntl::net::http3::frame_type type,
                  std::span<const std::byte> payload) {
  if (!ntl::net::http3::append_quic_varint(
           wire, static_cast<std::uint64_t>(type))
           .is_ok() ||
      !ntl::net::http3::append_quic_varint(wire, payload.size()).is_ok())
    throw std::runtime_error("cannot encode controlled origin HTTP/3 frame");
  wire.insert(wire.end(), payload.begin(), payload.end());
}

class origin_connection final : public ntl::net::quic::backend_sink,
                                public ntl::net::http3::inspection_sink {
public:
  origin_connection(std::string server_name,
                    std::atomic<std::uint64_t> &requests,
                    std::atomic<std::uint64_t> &transformed,
                    std::atomic<std::uint64_t> &decoder_acknowledgements,
                    std::atomic<std::uint64_t> &qpack_encoder_streams)
      noexcept
      : server_name_(std::move(server_name)), requests_(&requests),
        transformed_(&transformed),
        decoder_acknowledgements_(&decoder_acknowledgements),
        qpack_encoder_streams_(&qpack_encoder_streams),
        qpack_(ntl::net::http3::dynamic_qpack_limits{
            .maximum_table_capacity = 4096,
            .maximum_blocked_streams = 8,
            .maximum_encoder_stream_buffer = 64 * 1024,
            .maximum_literal_size = 64 * 1024}),
        inspector_(qpack_,
                   {.maximum_concurrent_request_streams = 64,
                    .maximum_buffered_bytes_per_stream = 4 * 1024 * 1024,
                    .frames = {4 * 1024 * 1024}},
                   64 * 1024) {}

  ~origin_connection() {
    std::vector<std::jthread> scheduled;
    {
      std::lock_guard guard(scheduled_lock_);
      for (auto &worker : scheduled_)
        worker.request_stop();
      scheduled.swap(scheduled_);
    }
  }

  ntl::status
  attach(std::shared_ptr<backend_connection> connection) noexcept {
    try {
      backend_ = std::move(connection);
      if (!backend_)
        return STATUS_INVALID_PARAMETER;
      ntl::status status = backend_->prepare_qpack_decoder_stream();
      if (status.is_ok()) {
        std::uint64_t control = 0;
        status = backend_->open_unidirectional_stream(control);
        auto settings =
            ntl::net::http3::webtransport::encode_control_stream(false);
        if (status.is_ok() && settings)
          status = backend_->write_stream(
              control, ntl::net::scatter_view::from_contiguous(*settings),
              false);
        else if (!settings)
          status = settings.status();
      }
      if (status.is_ok()) {
        status = backend_->open_unidirectional_stream(qpack_encoder_stream_);
        constexpr std::array<std::byte, 1> encoder_stream_prefix{
            std::byte{static_cast<unsigned char>(
                ntl::net::http3::msquic_backend::
                    qpack_encoder_stream_type)}};
        if (status.is_ok())
          status = backend_->write_stream(
              qpack_encoder_stream_,
              ntl::net::scatter_view::from_contiguous(
                  encoder_stream_prefix),
              false);
      }
      if (!status.is_ok()) {
        backend_->stop();
        qpack_encoder_stream_ = invalid_stream_id;
        return status;
      }
      qpack_encoder_streams_->fetch_add(1, std::memory_order_relaxed);
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      if (backend_)
        backend_->stop();
      qpack_encoder_stream_ = invalid_stream_id;
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      if (backend_)
        backend_->stop();
      qpack_encoder_stream_ = invalid_stream_id;
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  void stop() noexcept {
    if (backend_)
      backend_->stop();
  }
  void drain() noexcept {
    if (!backend_)
      return;
    if (!backend_->drain().is_ok())
      backend_->drain_exact();
  }

  ntl::status on_connected(std::string_view alpn) noexcept override {
    return alpn == "h3" ? ntl::status::ok()
                        : ntl::status{STATUS_PROTOCOL_NOT_SUPPORTED};
  }
  ntl::status on_request_stream(std::uint64_t stream_id,
                                ntl::net::scatter_view bytes,
                                bool final) noexcept override {
    try {
      const ntl::status status =
          inspector_.consume_request_stream(stream_id, bytes, final, *this);
      if (status != STATUS_RETRY)
        return status;
      blocked_.insert(stream_id);
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }
  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view bytes) noexcept override {
    try {
      ntl::status status = inspector_.consume_qpack_encoder_stream(bytes);
      if (!status.is_ok())
        return status;
      for (auto current = blocked_.begin(); current != blocked_.end();) {
        status = inspector_.resume_request_stream(*current, *this);
        if (status == STATUS_RETRY) {
          ++current;
          continue;
        }
        if (!status.is_ok())
          return status;
        current = blocked_.erase(current);
      }
      auto acknowledgement = inspector_.take_qpack_decoder_stream();
      if (!acknowledgement)
        return acknowledgement.status();
      return acknowledgement->empty()
                 ? ntl::status::ok()
                 : backend_->write_qpack_decoder_stream(
                       ntl::net::scatter_view::from_contiguous(
                           *acknowledgement));
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }
  ntl::status on_peer_unidirectional_stream(
      std::uint64_t stream_id, ntl::net::scatter_view bytes,
      bool final) noexcept override {
    try {
      auto &wire = peer_unidirectional_[stream_id];
      if (bytes.size() > 4096 - wire.size())
        return STATUS_BUFFER_OVERFLOW;
      const std::size_t offset = wire.size();
      wire.resize(offset + bytes.size());
      if (bytes.size() != 0 &&
          !bytes.copy_to(std::span<std::byte>(wire).subspan(offset)).is_ok())
        return STATUS_DATA_ERROR;
      const auto view = ntl::net::scatter_view::from_contiguous(
          std::span<const std::byte>(wire));
      const auto type = ntl::net::http3::read_quic_varint(view);
      if (!type)
        return !final && type.status() == STATUS_BUFFER_TOO_SMALL
                   ? ntl::status::ok()
                   : type.status();
      if (type->value ==
              ntl::net::http3::msquic_backend::qpack_decoder_stream_type &&
          wire.size() > type->encoded_size &&
          acknowledged_streams_.insert(stream_id).second)
        decoder_acknowledgements_->fetch_add(1, std::memory_order_relaxed);
      if (final)
        peer_unidirectional_.erase(stream_id);
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }
  void on_closed(NTSTATUS) noexcept override {
    closed_.store(true, std::memory_order_release);
  }
  bool closed() const noexcept {
    return closed_.load(std::memory_order_acquire);
  }

  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    try {
      auto &request = streams_[stream_id];
      if (request.headers_seen)
        return STATUS_DATA_ERROR;
      request.headers_seen = true;
      for (const auto &field : fields) {
        if (field.name == ":method")
          request.method = field.value;
        else if (field.name == ":path")
          request.path = field.value;
        else if (field.name == ":authority")
          request.authority = field.value;
        else if (field.name == "content-type")
          request.content_type = field.value;
        else if (field.name == "x-ntl-inspected" &&
                 field.value == "1")
          request.transformed = true;
      }
      if (request.method.empty() || request.path.empty() ||
          request.authority.empty())
        return STATUS_DATA_ERROR;
      const std::string_view authority(request.authority);
      if (authority != server_name_ &&
          !authority.starts_with(server_name_ + ":"))
        return STATUS_INVALID_ADDRESS;
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }
  ntl::status on_data(std::uint64_t stream_id,
                      ntl::net::scatter_view data) noexcept override {
    try {
      auto found = streams_.find(stream_id);
      if (found == streams_.end() || !found->second.headers_seen ||
          data.size() > 4 * 1024 * 1024 - found->second.body.size())
        return STATUS_DATA_ERROR;
      const std::size_t offset = found->second.body.size();
      found->second.body.resize(offset + data.size());
      return data.size() == 0
                 ? ntl::status::ok()
                 : data.copy_to(std::span<std::byte>(found->second.body)
                                    .subspan(offset));
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }
  ntl::status on_stream_end(std::uint64_t stream_id) noexcept override {
    try {
      const auto found = streams_.find(stream_id);
      if (found == streams_.end() || !found->second.headers_seen)
        return STATUS_DATA_ERROR;
      request_state request = std::move(found->second);
      streams_.erase(found);
      ++*requests_;
      if (request.transformed)
        ++*transformed_;
      constexpr std::string_view multiplex_prefix = "/multiplex/";
      if (request.path.starts_with(multiplex_prefix) &&
          request.path.size() == multiplex_prefix.size() + 1 &&
          request.path.back() >= '1' && request.path.back() <= '4') {
        const unsigned ordinal =
            static_cast<unsigned>(request.path.back() - '0');
        std::lock_guard guard(scheduled_lock_);
        scheduled_.emplace_back(
            [this, stream_id, ordinal,
             request = std::move(request)](std::stop_token stop) {
              // Each multiplex request reaches the origin through an
              // independent QUIC connection.  Keep the controlled response
              // spacing larger than connection-setup jitter under Driver
              // Verifier so the reverse-completion contract is deterministic.
              const unsigned ticks = (5 - ordinal) * 100;
              for (unsigned tick = 0; tick != ticks; ++tick) {
                if (stop.stop_requested())
                  return;
                std::this_thread::sleep_for(10ms);
              }
              if (!stop.stop_requested())
                (void)send_response(stream_id, request);
            });
        return ntl::status::ok();
      }
      return send_response(stream_id, request);
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

private:
  struct request_state {
    std::string method;
    std::string path;
    std::string authority;
    std::string content_type;
    std::vector<std::byte> body;
    bool headers_seen = false;
    bool transformed = false;
  };

  ntl::status send_response(std::uint64_t stream_id,
                            const request_state &request) noexcept {
    try {
      unsigned status = 200;
      std::string content_encoding;
      const bool grpc = request.path == "/grpc";
      unsigned multiplex_ordinal = 0;
      constexpr std::string_view multiplex_prefix = "/multiplex/";
      if (request.path.starts_with(multiplex_prefix) &&
          request.path.size() == multiplex_prefix.size() + 1) {
        const char value = request.path.back();
        if (value >= '1' && value <= '4')
          multiplex_ordinal = static_cast<unsigned>(value - '0');
      }
      if (!request.transformed)
        status = 428;
      else if (request.path != "/allowed" && request.path != "/gzip" &&
               request.path != "/deflate" && request.path != "/br" &&
               request.path != "/blocked" && request.path != "/grpc" &&
               multiplex_ordinal == 0)
        status = 404;
      else if (grpc &&
               (request.method != "POST" ||
                request.content_type != "application/grpc" ||
                !grpc_payload_equals(
                    request.body, "ntl-grpc-transform|request")))
        status = 428;
      if (request.path == "/gzip")
        content_encoding = "gzip";
      else if (request.path == "/deflate")
        content_encoding = "deflate";
      else if (request.path == "/br")
        content_encoding = "br";

      const std::string html =
          status == 200
              ? "<!doctype html><html><body>controlled origin h3 "
                "request transformed by kernel</body></html>"
              : "<!doctype html><html><body>controlled origin rejected "
                "request</body></html>";
      std::vector<std::byte> body =
          grpc && status == 200 ? request.body : bytes_of(html);
      if (!content_encoding.empty()) {
        ntl::net::inspection::content_encoder_registry encoders;
        ntl::net::inspection::register_standard_content_encoders(encoders);
        auto encoded = ntl::net::inspection::encode_content_encoding(
            encoders, std::as_bytes(std::span(html)), content_encoding,
            {.maximum_input_size = 64 * 1024,
             .maximum_encoded_size = 64 * 1024,
             .maximum_coding_layers = 1});
        if (!encoded)
          return encoded.status();
        body = std::move(*encoded);
      }

      std::vector<ntl::net::http3::header_field> fields{
          {":status", std::to_string(status)},
          {"content-type", grpc && status == 200
                               ? "application/grpc"
                               : "text/html; charset=utf-8"},
          {"content-length", std::to_string(body.size())},
          {"x-controlled-origin", "h3"}};
      if (!content_encoding.empty())
        fields.push_back({"content-encoding", content_encoding});
      ntl::net::http3::bounded_static_qpack_encoder encoder;
      auto encoded_headers = encoder.encode(fields, 64 * 1024);
      if (!encoded_headers || encoded_headers->size() < 2)
        return encoded_headers ? ntl::status{STATUS_DATA_ERROR}
                               : encoded_headers.status();

      // Add one dynamic-table reference. The proxy must process the separate
      // encoder stream, unblock the response field section, and acknowledge
      // it before closing the validated origin connection.
      (*encoded_headers)[0] = std::byte{0x02};
      (*encoded_headers)[1] = std::byte{0x00};
      encoded_headers->push_back(std::byte{0x80});
      constexpr std::array<std::byte, 6> instructions{
          std::byte{0x3f}, std::byte{0x21},
          std::byte{0x41}, std::byte{'x'}, std::byte{0x01},
          std::byte{'y'}};
      std::vector<std::byte> wire;
      wire.reserve(encoded_headers->size() + body.size() + 32);
      append_frame(wire, ntl::net::http3::frame_type::headers,
                   *encoded_headers);
      append_frame(wire, ntl::net::http3::frame_type::data, body);
      std::lock_guard write_guard(qpack_write_lock_);
      if (!backend_ || qpack_encoder_stream_ == invalid_stream_id)
        return STATUS_INVALID_DEVICE_STATE;
      ntl::status result = backend_->write_stream(
          qpack_encoder_stream_,
          ntl::net::scatter_view::from_contiguous(instructions), false);
      if (result.is_ok())
        result = backend_->write_stream(
            stream_id, ntl::net::scatter_view::from_contiguous(wire), true);
      return result;
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  std::string server_name_;
  std::atomic<std::uint64_t> *requests_ = nullptr;
  std::atomic<std::uint64_t> *transformed_ = nullptr;
  std::atomic<std::uint64_t> *decoder_acknowledgements_ = nullptr;
  std::atomic<std::uint64_t> *qpack_encoder_streams_ = nullptr;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_dynamic_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::shared_ptr<backend_connection> backend_;
  std::unordered_map<std::uint64_t, request_state> streams_;
  std::unordered_set<std::uint64_t> blocked_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>>
      peer_unidirectional_;
  std::unordered_set<std::uint64_t> acknowledged_streams_;
  static constexpr std::uint64_t invalid_stream_id =
      (std::numeric_limits<std::uint64_t>::max)();
  std::uint64_t qpack_encoder_stream_ = invalid_stream_id;
  std::mutex qpack_write_lock_;
  std::mutex scheduled_lock_;
  std::vector<std::jthread> scheduled_;
  std::atomic<bool> closed_{false};
};

} // namespace

class managed_http3_origin::implementation {
public:
  implementation(
      const std::array<
          std::byte,
          wfp_kernel_browser_https_inspection::certificate_thumbprint_size>
          &thumbprint,
      std::string server_name)
      : registration_(library_.api()),
        configuration_(library_.api(), registration_.get(), thumbprint),
        server_name_(std::move(server_name)) {
    if (server_name_.empty())
      throw std::invalid_argument("managed origin server name is empty");
    require_quic(library_.api()->ListenerOpen(
                     registration_.get(), &listener_callback, this,
                     &listener_),
                 "ListenerOpen(controlled origin) failed");
    QUIC_BUFFER alpn{2, reinterpret_cast<std::uint8_t *>(
                           (const_cast<char *>("h3")))};
    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&address, 0);
    require_quic(library_.api()->ListenerStart(
                     listener_, &alpn, 1, &address),
                 "ListenerStart(controlled origin) failed");
    std::uint32_t size = sizeof(address);
    require_quic(library_.api()->GetParam(
                     listener_, QUIC_PARAM_LISTENER_LOCAL_ADDRESS, &size,
                     &address),
                 "GetParam(controlled origin address) failed");
    port_ = QuicAddrGetPort(&address);
    if (port_ == 0)
      throw std::runtime_error("controlled origin did not bind a port");
    reaper_ = std::jthread(
        [this](std::stop_token stop) noexcept {
          while (!stop.stop_requested()) {
            reap_closed_connections();
            std::this_thread::sleep_for(10ms);
          }
          reap_closed_connections();
        });
  }

  implementation(const implementation &) = delete;
  implementation &operator=(const implementation &) = delete;
  ~implementation() { stop(); }

  std::uint16_t port() const noexcept { return port_; }
  std::uint64_t accepted() const noexcept {
    return accepted_.load(std::memory_order_relaxed);
  }
  std::uint64_t requests() const noexcept {
    return requests_.load(std::memory_order_relaxed);
  }
  std::uint64_t transformed_requests() const noexcept {
    return transformed_.load(std::memory_order_relaxed);
  }
  std::uint64_t decoder_acknowledgements() const noexcept {
    return decoder_acknowledgements_.load(std::memory_order_relaxed);
  }
  std::uint64_t qpack_encoder_streams() const noexcept {
    return qpack_encoder_streams_.load(std::memory_order_relaxed);
  }
  std::uint64_t active_connections() noexcept {
    // A connection can close after the last listener NEW_CONNECTION event.
    // Reap from the acceptance thread as well so the reported count reflects
    // live native connections rather than closed objects awaiting teardown.
    reap_closed_connections();
    std::lock_guard guard(lock_);
    return connections_.size();
  }

private:
  static QUIC_STATUS QUIC_API listener_callback(
      HQUIC, void *context, QUIC_LISTENER_EVENT *event) noexcept {
    auto *self = static_cast<implementation *>(context);
    if (!self || !event)
      return QUIC_STATUS_INVALID_PARAMETER;
    if (event->Type == QUIC_LISTENER_EVENT_DOS_MODE_CHANGED)
      return QUIC_STATUS_SUCCESS;
    if (event->Type == QUIC_LISTENER_EVENT_STOP_COMPLETE) {
      {
        std::lock_guard guard(self->lock_);
        self->listener_stopped_ = true;
      }
      self->changed_.notify_all();
      return QUIC_STATUS_SUCCESS;
    }
    if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION ||
        !event->NEW_CONNECTION.Connection)
      return QUIC_STATUS_SUCCESS;
    if (!event->NEW_CONNECTION.Info)
      return QUIC_STATUS_INVALID_PARAMETER;
    // Never wait for a previous connection to drain from MsQuic's
    // NEW_CONNECTION callback. Multiplexed clients can arrive while a closed
    // connection still has shutdown callbacks in flight; synchronously
    // reaping it here stalls listener dispatch and makes unrelated handshakes
    // time out. The acceptance thread reaps for observations, and stop()
    // owns the final deterministic drain.
    try {
      auto connection = std::make_unique<origin_connection>(
          self->server_name_, self->requests_, self->transformed_,
          self->decoder_acknowledgements_,
          self->qpack_encoder_streams_);

      // Reserve the bounded session slot before installing object-backed
      // callbacks. Returning CONNECTION_REFUSED before adoption lets MsQuic
      // release the native handle itself. Once adopted, the session remains
      // owned until SHUTDOWN_COMPLETE and is drained outside this callback.
      origin_connection *const session = connection.get();
      {
        std::lock_guard guard(self->lock_);
        const std::size_t active = static_cast<std::size_t>(std::count_if(
            self->connections_.begin(), self->connections_.end(),
            [](const auto &current) noexcept {
              return !current->closed();
            }));
        if (self->stopping_ || active >= 128)
          return QUIC_STATUS_CONNECTION_REFUSED;
        self->connections_.push_back(std::move(connection));
      }

      auto indication = ntl::net::http3::msquic_backend::
          borrowed_accepted_connection::from_native(
              event->NEW_CONNECTION.Connection,
              *event->NEW_CONNECTION.Info);
      auto accepted = backend_connection::try_accept_borrowed(
          self->library_.api(), std::move(indication),
          self->configuration_.get(), *session,
          {.maximum_streams = 128,
           .maximum_receive_indication = 128 * 1024,
           .maximum_send_size = 4 * 1024 * 1024,
           .maximum_prefix_bytes = 8,
           .shutdown_timeout = 10s});
      if (!accepted) {
        std::unique_ptr<origin_connection> rejected;
        {
          std::lock_guard guard(self->lock_);
          const auto found = std::find_if(
              self->connections_.begin(), self->connections_.end(),
              [session](const auto &current) noexcept {
                return current.get() == session;
              });
          if (found != self->connections_.end()) {
            rejected = std::move(*found);
            self->connections_.erase(found);
          }
        }
        return static_cast<QUIC_STATUS>(accepted.status());
      }
      const ntl::status attached =
          session->attach(std::move(*accepted));
      if (!attached.is_ok()) {
        // Configuration succeeded, so returning listener failure would make
        // MsQuic reclaim the handle without delivering the shutdown callback
        // expected by the adopted wrapper. Keep ownership and finish the
        // asynchronous shutdown through the reaper instead.
        session->stop();
        return QUIC_STATUS_SUCCESS;
      }
      self->accepted_.fetch_add(1, std::memory_order_relaxed);
      return QUIC_STATUS_SUCCESS;
    } catch (const std::bad_alloc &) {
      return QUIC_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      return QUIC_STATUS_INTERNAL_ERROR;
    }
  }

  void reap_closed_connections() noexcept {
    for (;;) {
      std::unique_ptr<origin_connection> closed;
      {
        std::lock_guard guard(lock_);
        const auto found = std::find_if(
            connections_.begin(), connections_.end(),
            [](const auto &connection) noexcept {
              return connection->closed();
            });
        if (found == connections_.end())
          return;
        closed = std::move(*found);
        connections_.erase(found);
      }
      closed->drain();
    }
  }

  void stop() noexcept {
    std::vector<std::unique_ptr<origin_connection>> connections;
    {
      std::lock_guard guard(lock_);
      if (stopping_)
        return;
      stopping_ = true;
    }
    if (listener_) {
      library_.api()->ListenerStop(listener_);
      {
        std::unique_lock lock(lock_);
        (void)changed_.wait_for(
            lock, 10s, [this] { return listener_stopped_; });
      }
      library_.api()->ListenerClose(listener_);
      listener_ = nullptr;
    }
    reaper_.request_stop();
    if (reaper_.joinable())
      reaper_.join();
    {
      std::lock_guard guard(lock_);
      connections.swap(connections_);
    }
    for (auto &connection : connections)
      connection->stop();
    library_.api()->RegistrationShutdown(
        registration_.get(), QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, 0);
    for (auto &connection : connections)
      connection->drain();
  }

  loaded_msquic library_;
  registration_owner registration_;
  server_configuration configuration_;
  std::string server_name_;
  HQUIC listener_ = nullptr;
  std::uint16_t port_ = 0;
  mutable std::mutex lock_;
  std::condition_variable changed_;
  std::vector<std::unique_ptr<origin_connection>> connections_;
  std::jthread reaper_;
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> requests_{0};
  std::atomic<std::uint64_t> transformed_{0};
  std::atomic<std::uint64_t> decoder_acknowledgements_{0};
  std::atomic<std::uint64_t> qpack_encoder_streams_{0};
  bool stopping_ = false;
  bool listener_stopped_ = false;
};

managed_http3_origin::managed_http3_origin(
    const std::array<
        std::byte,
        wfp_kernel_browser_https_inspection::certificate_thumbprint_size>
        &certificate_thumbprint,
    std::string server_name)
    : implementation_(std::make_unique<implementation>(
          certificate_thumbprint, std::move(server_name))) {}

managed_http3_origin::~managed_http3_origin() = default;

std::uint16_t managed_http3_origin::port() const noexcept {
  return implementation_->port();
}
std::uint64_t managed_http3_origin::accepted() const noexcept {
  return implementation_->accepted();
}
std::uint64_t managed_http3_origin::requests() const noexcept {
  return implementation_->requests();
}
std::uint64_t managed_http3_origin::transformed_requests() const noexcept {
  return implementation_->transformed_requests();
}
std::uint64_t managed_http3_origin::decoder_acknowledgements() const noexcept {
  return implementation_->decoder_acknowledgements();
}
std::uint64_t managed_http3_origin::qpack_encoder_streams() const noexcept {
  return implementation_->qpack_encoder_streams();
}
std::uint64_t managed_http3_origin::active_connections() const noexcept {
  return implementation_->active_connections();
}

} // namespace crtsys::wfp_kernel_browser_https
