#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef QUIC_API_ENABLE_PREVIEW_FEATURES
#error The managed HTTP/3 acceptance client requires MsQuic preview reliable-reset support.
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include "managed_http3_client.hpp"

#include <msquic.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ntl/net/http3/backend>
#include <ntl/net/grpc/framing>
#include <ntl/net/http3/framing>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/msquic_runtime>
#include <ntl/net/http3/qpack>
#include <ntl/net/http3/webtransport_session>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/standard_content_decoders>

namespace crtsys::wfp_kernel_browser_https {
namespace {

using backend_connection = ntl::net::http3::msquic_backend::connection;
using backend_target = ntl::net::http3::msquic_backend::connection_target;
using namespace std::chrono_literals;

// The service may spend its bounded 10-second QUIC origin deadline before it
// performs an allowed H2/H1 retry. The end-to-end client must observe that
// complete policy decision rather than race the first-attempt deadline.
inline constexpr auto controlled_response_timeout = 30s;

sockaddr_in6 routed_ipv6_test_endpoint(std::uint16_t port) {
  ULONG bytes = 16 * 1024;
  std::vector<std::byte> storage(bytes);
  ULONG status = ERROR_BUFFER_OVERFLOW;
  for (unsigned attempt = 0; attempt != 3; ++attempt) {
    auto *adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    status = ::GetAdaptersAddresses(
        AF_INET6,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &bytes);
    if (status != ERROR_BUFFER_OVERFLOW)
      break;
    storage.resize(bytes);
  }
  if (status != NO_ERROR)
    throw std::system_error(static_cast<int>(status), std::system_category(),
                            "GetAdaptersAddresses(controlled HTTP/3 IPv6)");

  auto *adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
  for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp ||
        adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    for (auto *unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
      if (!unicast->Address.lpSockaddr ||
          unicast->Address.lpSockaddr->sa_family != AF_INET6 ||
          unicast->Address.iSockaddrLength < sizeof(sockaddr_in6) ||
          unicast->OnLinkPrefixLength >= 128)
        continue;
      sockaddr_in6 candidate = *reinterpret_cast<const sockaddr_in6 *>(
          unicast->Address.lpSockaddr);
      if (IN6_IS_ADDR_UNSPECIFIED(&candidate.sin6_addr) ||
          IN6_IS_ADDR_LOOPBACK(&candidate.sin6_addr) ||
          IN6_IS_ADDR_MULTICAST(&candidate.sin6_addr))
        continue;

      // Preserve the on-link prefix and alter its first host bit. WFP absorbs
      // the datagram before neighbor discovery or external transmission.
      const unsigned host_bit = unicast->OnLinkPrefixLength;
      candidate.sin6_addr.u.Byte[host_bit / 8] ^=
          static_cast<unsigned char>(0x80u >> (host_bit % 8));
      candidate.sin6_port = htons(port);
      if (candidate.sin6_scope_id == 0)
        candidate.sin6_scope_id = adapter->Ipv6IfIndex;
      return candidate;
    }
  }
  throw std::runtime_error(
      "no routed non-loopback IPv6 prefix is available for HTTP/3 acceptance");
}

backend_target controlled_wfp_target(int address_family,
                                     std::uint16_t port) {
  QUIC_ADDRESS_FAMILY family = QUIC_ADDRESS_FAMILY_UNSPEC;
  QUIC_ADDR remote{};
  if (address_family == AF_INET) {
    family = QUIC_ADDRESS_FAMILY_INET;
    remote.Ipv4.sin_family = AF_INET;
    remote.Ipv4.sin_port = htons(port);
    if (::InetPtonA(AF_INET, "192.0.2.1", &remote.Ipv4.sin_addr) != 1)
      throw std::runtime_error("cannot construct controlled HTTP/3 endpoint");
  } else if (address_family == AF_INET6) {
    family = QUIC_ADDRESS_FAMILY_INET6;
    remote.Ipv6 = routed_ipv6_test_endpoint(port);
  } else {
    throw std::invalid_argument("unsupported controlled HTTP/3 family");
  }
  return {.server_name = "localhost",
          .port = port,
          .family = family,
          .remote_address = remote};
}

backend_target direct_loopback_target(int address_family,
                                      std::uint16_t port) {
  QUIC_ADDRESS_FAMILY family = QUIC_ADDRESS_FAMILY_UNSPEC;
  QUIC_ADDR remote{};
  if (address_family == AF_INET) {
    family = QUIC_ADDRESS_FAMILY_INET;
    remote.Ipv4.sin_family = AF_INET;
    remote.Ipv4.sin_port = htons(port);
    remote.Ipv4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (address_family == AF_INET6) {
    family = QUIC_ADDRESS_FAMILY_INET6;
    remote.Ipv6.sin6_family = AF_INET6;
    remote.Ipv6.sin6_port = htons(port);
    remote.Ipv6.sin6_addr = in6addr_loopback;
  } else {
    throw std::invalid_argument("unsupported direct HTTP/3 family");
  }
  return {.server_name = "localhost",
          .port = port,
          .family = family,
          .remote_address = remote};
}

std::string status_hex(NTSTATUS status) {
  std::array<char, 11> text{};
  text[0] = '0';
  text[1] = 'x';
  const auto converted = std::to_chars(
      text.data() + 2, text.data() + text.size() - 1,
      static_cast<std::uint32_t>(status), 16);
  if (converted.ec != std::errc{})
    return "<unavailable>";
  return std::string(text.data(), converted.ptr);
}

class client_configuration {
public:
  explicit client_configuration(
      std::span<const std::byte> certificate_thumbprint = {}) {
    const ntl::status opened = runtime_.open(
        "crtsys-kernel-http3-client",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY);
    if (!opened.is_ok())
      throw std::runtime_error("MsQuic runtime open failed");

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
    constexpr std::array<std::string_view, 1> protocols{"h3"};
    const ntl::status configured =
        configuration_.open(runtime_, protocols, &settings);
    if (!configured.is_ok())
      throw std::runtime_error("MsQuic configuration open failed");

    QUIC_CERTIFICATE_HASH_STORE hash{};
    QUIC_CREDENTIAL_CONFIG credentials{};
    if (certificate_thumbprint.empty()) {
      credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    } else {
      if (certificate_thumbprint.size() != sizeof(hash.ShaHash))
        throw std::invalid_argument(
            "HTTP/3 client certificate thumbprint has an invalid size");
      std::memcpy(hash.ShaHash, certificate_thumbprint.data(),
                  certificate_thumbprint.size());
      std::memcpy(hash.StoreName, "MY", 2);
      hash.Flags = QUIC_CERTIFICATE_HASH_STORE_FLAG_MACHINE_STORE;
      credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
      credentials.CertificateHashStore = &hash;
    }
    credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    const ntl::status loaded = configuration_.load_credential(credentials);
    if (!loaded.is_ok())
      throw std::runtime_error("MsQuic credential load failed");
  }

  ntl::net::http3::msquic_backend::connection_context
  connection_context() const noexcept {
    return configuration_.make_connection_context();
  }

private:
  // Intentionally declared in the inconvenient order. The owning
  // configuration retains the runtime state, so facade declaration order is
  // not part of the acceptance client's correctness contract.
  ntl::net::http3::msquic_backend::configuration configuration_;
  ntl::net::http3::msquic_backend::runtime runtime_;
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
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
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
    try {
      std::lock_guard guard(lock_);
      for (const auto &field : fields) {
        if (field.name == ":status") {
          unsigned status = 0;
          const auto parsed = std::from_chars(
              field.value.data(), field.value.data() + field.value.size(),
              status);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != field.value.data() + field.value.size() ||
              status < 100 || status > 599)
            return STATUS_DATA_ERROR;
          result_.status = status;
        } else if (field.name == "content-encoding") {
          result_.content_encoding = field.value;
        }
      }
      return result_.status ? ntl::status::ok()
                            : ntl::status{STATUS_DATA_ERROR};
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view data) noexcept override {
    if (data.size() > 64 * 1024)
      return STATUS_BUFFER_OVERFLOW;
    try {
      std::vector<std::byte> copied(data.size());
      if (!copied.empty() && !data.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      std::lock_guard guard(lock_);
      if (copied.size() > 64 * 1024 - result_.body.size())
        return STATUS_BUFFER_OVERFLOW;
      result_.body.insert(result_.body.end(), copied.begin(), copied.end());
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
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
    std::unique_lock lock(lock_);
    const bool signaled = changed_.wait_for(lock, 10s, [this] {
      return connected_ || closed_ ||
             callback_failure_.load(std::memory_order_acquire) !=
                 STATUS_SUCCESS;
    });
    if (!connected_) {
      throw std::runtime_error(
          std::string("kernel HTTP/3 handshake did not complete signaled=") +
          (signaled ? "1" : "0") + " closed=" + (closed_ ? "1" : "0") +
          " close-status=" + status_hex(close_status_) +
          " callback-status=" + status_hex(callback_failure_.load(
                                     std::memory_order_acquire)));
    }
  }

  void send_request(std::string_view method, std::string_view path,
                    bool block, bool dynamic_qpack,
                    std::string_view authority,
                    std::string_view content_type,
                    std::span<const std::byte> body) {
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
        {":method", std::string(method)},
        {":scheme", "https"},
        {":authority", std::string(authority)},
        {":path", std::string(path)},
    };
    if (block)
      fields.push_back({"x-ntl-block", "1"});
    if (!content_type.empty()) {
      fields.push_back({"content-type", std::string(content_type)});
      fields.push_back({"content-length", std::to_string(body.size())});
    }
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
    if (!body.empty()) {
      if (!ntl::net::http3::append_quic_varint(
               wire, static_cast<std::uint64_t>(
                         ntl::net::http3::frame_type::data))
               .is_ok() ||
          !ntl::net::http3::append_quic_varint(wire, body.size()).is_ok())
        throw std::runtime_error("cannot frame HTTP/3 request body");
      wire.insert(wire.end(), body.begin(), body.end());
    }
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
    const bool signaled =
        changed_.wait_for(lock, controlled_response_timeout, [this] {
          return (complete_ &&
                  (!expect_qpack_ack_ || qpack_acknowledged_)) ||
                 closed_ || callback_failure_.load(
                                 std::memory_order_acquire) != STATUS_SUCCESS;
        });
    if (!signaled || !complete_ ||
        (expect_qpack_ack_ && !qpack_acknowledged_)) {
      throw std::runtime_error(
          std::string("kernel HTTP/3 response did not complete signaled=") +
          (signaled ? "1" : "0") + " complete=" +
          (complete_ ? "1" : "0") + " qpack-required=" +
          (expect_qpack_ack_ ? "1" : "0") + " qpack-ack=" +
           (qpack_acknowledged_ ? "1" : "0") + " closed=" +
           (closed_ ? "1" : "0") + " close-status=" +
           status_hex(close_status_) + " callback-status=" +
           status_hex(callback_failure_.load(std::memory_order_acquire)) +
           " response-status=" + std::to_string(result_.status) +
           " response-bytes=" + std::to_string(result_.body.size()));
    }
    return result_;
  }

private:
  ntl::status fail_callback(NTSTATUS status) noexcept {
    callback_failure_.store(status, std::memory_order_release);
    changed_.notify_all();
    return status;
  }

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
  std::atomic<NTSTATUS> callback_failure_{STATUS_SUCCESS};
  bool connected_ = false;
  bool peer_settings_ready_ = false;
  bool expect_qpack_ack_ = false;
  bool qpack_acknowledged_ = false;
  bool complete_ = false;
  bool closed_ = false;
};

class multiplex_client_sink final
    : public ntl::net::quic::backend_sink,
      public ntl::net::http3::inspection_sink {
public:
  multiplex_client_sink() noexcept
      : inspector_(qpack_,
                   {.maximum_concurrent_request_streams = 16,
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
      if (connected_)
        ++connected_callbacks_;
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
      if (type->value !=
          ntl::net::http3::webtransport::control_stream_type) {
        if (final)
          peer_unidirectional_.erase(stream_id);
        return ntl::status::ok();
      }

      const auto settings =
          ntl::net::http3::webtransport::parse_control_stream(view);
      if (!settings) {
        if (!final && (settings.status() == STATUS_BUFFER_TOO_SMALL ||
                       settings.status() == STATUS_END_OF_FILE))
          return ntl::status::ok();
        return settings.status();
      }
      peer_settings_ = *settings;
      peer_settings_ready_ = peer_settings_.server_ready();
      peer_unidirectional_.erase(stream_id);
      changed_.notify_all();
      return peer_settings_ready_ ? ntl::status::ok()
                                  : ntl::status{STATUS_PROTOCOL_NOT_SUPPORTED};
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
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

  ntl::status on_peer_receive_aborted(
      std::uint64_t stream_id, std::uint64_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      if (quota_streams_.contains(stream_id))
        quota_observed_ = true;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_headers(
      std::uint64_t stream_id,
      std::span<const ntl::net::http3::header_field> fields) noexcept override {
    try {
      std::lock_guard guard(lock_);
      auto found = responses_.find(stream_id);
      if (found == responses_.end() || found->second.headers_seen)
        return STATUS_DATA_ERROR;
      auto &slot = found->second;
      slot.headers_seen = true;
      for (const auto &field : fields) {
        if (field.name == ":status") {
          if (field.value.size() != 3)
            return STATUS_DATA_ERROR;
          unsigned status = 0;
          for (const char digit : field.value) {
            if (digit < '0' || digit > '9')
              return STATUS_DATA_ERROR;
            status = status * 10 + static_cast<unsigned>(digit - '0');
          }
          slot.value.status = status;
        } else if (field.name == "content-encoding") {
          slot.value.content_encoding = field.value;
        }
      }
      return slot.value.status ? ntl::status::ok()
                               : ntl::status{STATUS_DATA_ERROR};
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::status on_data(std::uint64_t stream_id,
                      ntl::net::scatter_view data) noexcept override {
    try {
      std::vector<std::byte> copied(data.size());
      if (!copied.empty() && !data.copy_to(copied).is_ok())
        return STATUS_DATA_ERROR;
      std::lock_guard guard(lock_);
      auto found = responses_.find(stream_id);
      if (found == responses_.end() ||
          copied.size() > 64 * 1024 - found->second.value.body.size())
        return STATUS_BUFFER_OVERFLOW;
      found->second.value.body.insert(found->second.value.body.end(),
                                      copied.begin(), copied.end());
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::status on_stream_end(std::uint64_t stream_id) noexcept override {
    try {
      std::lock_guard guard(lock_);
      auto found = responses_.find(stream_id);
      if (found == responses_.end() || found->second.complete ||
          !found->second.headers_seen)
        return STATUS_DATA_ERROR;
      found->second.complete = true;
      completion_order_.push_back(found->second.ordinal);
      ++completed_;
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  void wait_connected() {
    std::unique_lock lock(lock_);
    if (!changed_.wait_for(lock, 10s,
                           [this] { return connected_ || closed_; }) ||
        !connected_)
      throw std::runtime_error("multiplex HTTP/3 handshake timed out");
  }

  void send_requests(std::string_view authority) {
    if (!connection_)
      throw std::logic_error("multiplex HTTP/3 backend is not attached");
    std::uint64_t control = 0;
    auto settings =
        ntl::net::http3::webtransport::encode_control_stream(false);
    if (!settings ||
        !connection_->open_unidirectional_stream(control).is_ok() ||
        !connection_->write_stream(
                         control,
                         ntl::net::scatter_view::from_contiguous(*settings),
                         false)
             .is_ok())
      throw std::runtime_error("cannot send multiplex HTTP/3 SETTINGS");

    struct request_specification {
      std::string_view path;
      unsigned ordinal;
      bool block;
      bool reset;
    };
    constexpr std::array requests{
        request_specification{"/multiplex/1", 1, false, false},
        request_specification{"/multiplex/2", 2, false, false},
        request_specification{"/blocked", 0, true, false},
        request_specification{"/allowed", 99, false, true},
        request_specification{"/multiplex/3", 3, false, false},
        request_specification{"/multiplex/4", 4, false, false},
        request_specification{"/allowed", 5, false, false}};
    ntl::net::http3::bounded_static_qpack_encoder encoder;
    for (const auto &request : requests) {
      std::vector<ntl::net::http3::header_field> fields{
          {":method", "GET"},
          {":scheme", "https"},
          {":authority", std::string(authority)},
          {":path", std::string(request.path)}};
      if (request.block)
        fields.push_back({"x-ntl-block", "1"});
      auto headers = encoder.encode(fields, 16 * 1024);
      if (!headers)
        throw std::runtime_error("cannot encode multiplex request");
      std::vector<std::byte> wire;
      if (!ntl::net::http3::append_quic_varint(
               wire, static_cast<std::uint64_t>(
                         ntl::net::http3::frame_type::headers))
               .is_ok() ||
          !ntl::net::http3::append_quic_varint(wire, headers->size()).is_ok())
        throw std::runtime_error("cannot frame multiplex request");
      wire.insert(wire.end(), headers->begin(), headers->end());
      std::uint64_t stream_id = 0;
      if (!connection_->open_request_stream(stream_id).is_ok())
        throw std::runtime_error("cannot open multiplex request stream");
      if (!request.reset) {
        std::lock_guard guard(lock_);
        responses_.emplace(stream_id,
                           response_slot{.ordinal = request.ordinal});
      }
      if (!connection_->write_stream(
                           stream_id,
                           ntl::net::scatter_view::from_contiguous(wire),
                           !request.reset)
               .is_ok())
        throw std::runtime_error("cannot send multiplex request stream");
      if (request.reset) {
        if (!connection_->reset_stream(stream_id, 0x123).is_ok())
          throw std::runtime_error("cannot reset multiplex request stream");
        reset_sent_ = true;
      }
    }
    request_streams_ = requests.size();
    expected_responses_ = requests.size() - 1;
  }

  http3_multiplex_result wait_result() {
    std::unique_lock lock(lock_);
    const bool signaled = changed_.wait_for(lock, controlled_response_timeout,
                                            [this] {
          return (completed_ == expected_responses_ &&
                  peer_settings_ready_) ||
                 closed_ || callback_failure_.load(
                                 std::memory_order_acquire) != STATUS_SUCCESS;
        });
    if (!signaled || completed_ != expected_responses_ ||
        !peer_settings_ready_) {
      std::string detail =
          "multiplex HTTP/3 responses timed out completed=" +
          std::to_string(completed_) + "/" +
          std::to_string(expected_responses_) + " settings=" +
          std::to_string(peer_settings_ready_) + " closed=" +
          std::to_string(closed_) + " callback=0x";
      constexpr char hex[] = "0123456789abcdef";
      const auto callback = static_cast<std::uint32_t>(
          callback_failure_.load(std::memory_order_acquire));
      for (int shift = 28; shift >= 0; shift -= 4)
        detail.push_back(hex[(callback >> shift) & 0xf]);
      detail += " streams=";
      for (const auto &[stream_id, slot] : responses_) {
        detail += std::to_string(stream_id) + ":h" +
                  std::to_string(slot.headers_seen) + "c" +
                  std::to_string(slot.complete) + "s" +
                  std::to_string(slot.value.status) + ",";
      }
      std::cerr << "[kernel-browser][h3][multiplex] " << detail
                << std::endl;
      throw std::runtime_error(detail);
    }
    // The four controlled delayed responses must complete in reverse ordinal
    // order.  The independent ordinary /allowed response is intentionally
    // undelayed, but its QUIC connection can finish anywhere among them under
    // Driver Verifier and is therefore not part of this ordering contract.
    const std::array<unsigned, 4> expected_order{4, 3, 2, 1};
    bool transformed = true;
    bool block = false;
    unsigned permitted = 0;
    for (const auto &[stream_id, slot] : responses_) {
      (void)stream_id;
      const std::string_view body(
          reinterpret_cast<const char *>(slot.value.body.data()),
          slot.value.body.size());
      if (slot.ordinal == 0) {
        block = slot.value.status == 403 &&
                body.find("blocked by browser inspection policy") !=
                    std::string_view::npos;
      } else {
        ++permitted;
        transformed = transformed && slot.value.status == 200 &&
                      body.find("inspected and transformed by ntl") !=
                          std::string_view::npos;
      }
    }
    std::vector<unsigned> delayed_completion_order;
    for (const unsigned ordinal : completion_order_) {
      if (ordinal >= 1 && ordinal <= 4)
        delayed_completion_order.push_back(ordinal);
    }
    std::string completion_order_text;
    for (const unsigned ordinal : completion_order_) {
      if (!completion_order_text.empty())
        completion_order_text.push_back(',');
      completion_order_text += std::to_string(ordinal);
    }
    return {.one_connection = connected_callbacks_ == 1,
            .peer_settings = peer_settings_ready_,
            .concurrent_streams = request_streams_ == 7 && permitted == 5,
            .reverse_completion =
                delayed_completion_order ==
                std::vector<unsigned>(expected_order.begin(),
                                      expected_order.end()),
            .stream_local_block = block,
            .stream_local_reset = reset_sent_,
            .transformed = transformed,
            .clean_drain = false,
            .request_streams = request_streams_,
            .completion_order = std::move(completion_order_text)};
  }

  bool exercise_aggregate_quota(std::string_view authority) {
    if (!connection_)
      throw std::logic_error("quota HTTP/3 backend is not attached");
    // Keep every request comfortably below the 512 KiB per-stream body bound
    // while the aggregate retained body exceeds the distinct 2 MiB service
    // quota.  Round-robin writes prevent transport scheduling from driving a
    // single stream to another limit before aggregate pressure is present.
    constexpr std::size_t payload_size = 48 * 1024;
    constexpr unsigned chunks_per_stream = 8;
    constexpr unsigned stream_count = 8;
    std::vector<std::byte> data_frame;
    if (!ntl::net::http3::append_quic_varint(
             data_frame, static_cast<std::uint64_t>(
                             ntl::net::http3::frame_type::data))
             .is_ok() ||
        !ntl::net::http3::append_quic_varint(data_frame, payload_size)
             .is_ok())
      throw std::runtime_error("cannot frame quota HTTP/3 DATA");
    data_frame.resize(data_frame.size() + payload_size, std::byte{0x51});

    std::vector<std::uint64_t> streams;
    streams.reserve(stream_count);
    ntl::net::http3::bounded_static_qpack_encoder encoder;
    for (unsigned index = 0; index != stream_count; ++index) {
      std::vector<ntl::net::http3::header_field> fields{
          {":method", "POST"},
          {":scheme", "https"},
          {":authority", std::string(authority)},
          {":path", "/quota/" + std::to_string(index)}};
      auto headers = encoder.encode(fields, 16 * 1024);
      if (!headers)
        throw std::runtime_error("cannot encode quota HTTP/3 request");
      std::vector<std::byte> head;
      if (!ntl::net::http3::append_quic_varint(
               head, static_cast<std::uint64_t>(
                         ntl::net::http3::frame_type::headers))
               .is_ok() ||
          !ntl::net::http3::append_quic_varint(head, headers->size()).is_ok())
        throw std::runtime_error("cannot frame quota HTTP/3 headers");
      head.insert(head.end(), headers->begin(), headers->end());
      std::uint64_t stream_id = 0;
      if (!connection_->open_request_stream(stream_id).is_ok())
        throw std::runtime_error("cannot open quota HTTP/3 stream");
      {
        std::lock_guard guard(lock_);
        quota_streams_.insert(stream_id);
      }
      streams.push_back(stream_id);
      if (!connection_->write_stream(
                           stream_id,
                           ntl::net::scatter_view::from_contiguous(head),
                           false)
               .is_ok())
        throw std::runtime_error("cannot send quota HTTP/3 headers");
    }
    for (unsigned chunk = 0; chunk != chunks_per_stream; ++chunk) {
      for (const std::uint64_t stream_id : streams) {
        const ntl::status written = connection_->write_stream(
            stream_id,
            ntl::net::scatter_view::from_contiguous(data_frame), false);
        if (!written.is_ok())
          continue;
      }
    }
    {
      std::unique_lock lock(lock_);
      (void)changed_.wait_for(lock, 10s, [this] {
        return quota_observed_ || closed_;
      });
    }
    // The service increments its quota counter before it aborts the rejected
    // stream. Allow already queued round-robin sends to settle before the
    // client resets the remaining streams during cleanup.
    if (quota_observed_)
      std::this_thread::sleep_for(500ms);
    for (const std::uint64_t stream_id : streams)
      (void)connection_->reset_stream(stream_id, 0x124);
    std::this_thread::sleep_for(250ms);
    std::lock_guard guard(lock_);
    return quota_observed_ && !closed_;
  }

private:
  ntl::status fail_callback(NTSTATUS status) noexcept {
    callback_failure_.store(status, std::memory_order_release);
    changed_.notify_all();
    return status;
  }

  struct response_slot {
    response value;
    unsigned ordinal = 0;
    bool headers_seen = false;
    bool complete = false;
  };

  backend_connection *connection_ = nullptr;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::unordered_map<std::uint64_t, response_slot> responses_;
  std::unordered_map<std::uint64_t, std::vector<std::byte>>
      peer_unidirectional_;
  std::unordered_set<std::uint64_t> quota_streams_;
  ntl::net::http3::webtransport::peer_settings peer_settings_{};
  std::vector<unsigned> completion_order_;
  std::mutex lock_;
  std::condition_variable changed_;
  NTSTATUS close_status_ = STATUS_SUCCESS;
  std::atomic<NTSTATUS> callback_failure_{STATUS_SUCCESS};
  std::size_t request_streams_ = 0;
  std::size_t completed_ = 0;
  std::size_t expected_responses_ = 0;
  std::size_t connected_callbacks_ = 0;
  bool connected_ = false;
  bool peer_settings_ready_ = false;
  bool closed_ = false;
  bool reset_sent_ = false;
  bool quota_observed_ = false;
};

class webtransport_client_sink final
    : public ntl::net::quic::backend_sink,
      public ntl::net::http3::inspection_sink {
public:
  webtransport_client_sink() noexcept
      : inspector_(qpack_,
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
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::status on_datagram(ntl::net::scatter_view bytes) noexcept override {
    try {
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
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::status on_peer_send_aborted(
      std::uint64_t stream_id, std::uint64_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      if (session_ && stream_id == session_->session_id())
        connect_stream_rejected_ = true;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_peer_receive_aborted(
      std::uint64_t stream_id, std::uint64_t) noexcept override {
    return on_peer_send_aborted(stream_id, 0);
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
    unsigned response_status = 0;
    bool status_seen = false;
    for (const auto &field : fields) {
      if (field.name != ":status")
        continue;
      if (status_seen)
        return STATUS_DATA_ERROR;
      const auto parsed = std::from_chars(
          field.value.data(), field.value.data() + field.value.size(),
          response_status);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != field.value.data() + field.value.size())
        return STATUS_DATA_ERROR;
      status_seen = true;
    }
    if (!status_seen || !session_ || session_->session_id() != stream_id)
      return STATUS_DATA_ERROR;
    const bool accepted = response_status >= 200 && response_status <= 299;
    const ntl::status applied =
        accepted ? session_->accept_client_response(stream_id, response_status)
                 : session_->reject_client_response(stream_id,
                                                    response_status);
    if (!applied.is_ok())
      return applied;
    {
      std::lock_guard guard(lock_);
      response_received_ = true;
      response_status_ = response_status;
      accepted_ = accepted;
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

  webtransport_result run(std::string_view authority) {
    if (!connection_ || !session_)
      throw std::logic_error("WebTransport client is not attached");
    negotiate();
    if (!session_->open_client({.authority = std::string(authority),
                                .path = "/webtransport",
                                .origin = "https://" + std::string(authority)})
             .is_ok())
      throw std::runtime_error("cannot send WebTransport Extended CONNECT");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s,
                             [this] {
                               return accepted_ || closed_ ||
                                      response_received_ ||
                                      callback_failure_.load(
                                          std::memory_order_acquire) !=
                                          STATUS_SUCCESS;
                             }) ||
          !accepted_) {
        if (response_received_)
          throw std::runtime_error(
              "WebTransport Extended CONNECT was rejected status=" +
              std::to_string(response_status_));
        throw std::runtime_error(
            "WebTransport Extended CONNECT timed out closed=" +
            std::to_string(closed_) + " callback=" +
            std::to_string(static_cast<std::uint32_t>(
                callback_failure_.load(std::memory_order_acquire))));
      }
    }
    constexpr std::string_view input = "client-payload";
    constexpr std::string_view expected = "ntl-inspected-payload";
    const auto payload = std::as_bytes(std::span(input));
    if (!session_->send_bidirectional(payload).is_ok() ||
        !session_->send_unidirectional(payload).is_ok() ||
        !session_->send_datagram(payload).is_ok())
      throw std::runtime_error("cannot send WebTransport payloads");

    auto first_capsule = ntl::net::http::encode_capsule(
        ntl::net::http3::webtransport::wt_drain_session, {}, {4096});
    const std::array<std::byte, 2> unknown_payload{
        std::byte{0x11}, std::byte{0x22}};
    auto second_capsule = ntl::net::http::encode_capsule(
        0x21, unknown_payload, {4096});
    auto third_capsule = ntl::net::http::encode_capsule(
        ntl::net::http3::webtransport::wt_drain_session, {}, {4096});
    if (!first_capsule || !second_capsule || !third_capsule ||
        first_capsule->size() < 2)
      throw std::runtime_error("cannot encode WebTransport Capsules");
    std::vector<std::byte> empty_data;
    std::vector<std::byte> split_data;
    std::vector<std::byte> coalesced_data;
    if (!ntl::net::http3::webtransport::session_detail::append_frame(
             empty_data,
             static_cast<std::uint64_t>(ntl::net::http3::frame_type::data),
             {})
             .is_ok() ||
        !ntl::net::http3::webtransport::session_detail::append_frame(
             split_data,
             static_cast<std::uint64_t>(ntl::net::http3::frame_type::data),
             std::span(*first_capsule).first(1))
             .is_ok())
      throw std::runtime_error("cannot encode split WebTransport DATA");
    std::vector<std::byte> capsule_tail(
        first_capsule->begin() + 1, first_capsule->end());
    capsule_tail.insert(capsule_tail.end(), second_capsule->begin(),
                        second_capsule->end());
    capsule_tail.insert(capsule_tail.end(), third_capsule->begin(),
                        third_capsule->end());
    if (!ntl::net::http3::webtransport::session_detail::append_frame(
             coalesced_data,
             static_cast<std::uint64_t>(ntl::net::http3::frame_type::data),
             capsule_tail)
             .is_ok() ||
        !connection_->write_stream(
                        session_->session_id(),
                        ntl::net::http3::webtransport::session_detail::scatter(
                            empty_data),
                        false)
             .is_ok() ||
        !connection_->write_stream(
                        session_->session_id(),
                        ntl::net::http3::webtransport::session_detail::scatter(
                            split_data),
                        false)
             .is_ok() ||
        !connection_->write_stream(
                        session_->session_id(),
                        ntl::net::http3::webtransport::session_detail::scatter(
                            coalesced_data),
                        false)
             .is_ok())
      throw std::runtime_error(
          "cannot send split/coalesced WebTransport Capsules");
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
                   closed_ || callback_failure_.load(
                                  std::memory_order_acquire) != STATUS_SUCCESS;
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
            .capsules = 3,
            .reliable_reset = true};
  }

  struct policy_rejection_result {
    bool rejected = false;
    bool client_inactive = false;
  };

  policy_rejection_result run_policy_rejection() {
    if (!connection_ || !session_)
      throw std::logic_error("WebTransport client is not attached");
    negotiate();
    if (!session_->open_client(
                     {.authority = "localhost",
                      .path = "/webtransport",
                      .origin = "https://localhost",
                      .additional_headers = {{"x-ntl-block", "1", false}}})
             .is_ok())
      throw std::runtime_error(
          "cannot send blocked WebTransport Extended CONNECT");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 2s, [this] {
            return response_received_ || connect_stream_rejected_ || closed_ ||
                   callback_failure_.load(std::memory_order_acquire) !=
                       STATUS_SUCCESS;
          }))
        throw std::runtime_error(
            "blocked WebTransport Extended CONNECT was not rejected promptly");
    }
    const bool rejected_response =
        response_received_ && !accepted_ && response_status_ >= 300 &&
        response_status_ <= 599;
    return {.rejected = rejected_response || connect_stream_rejected_,
            .client_inactive = !session_->active()};
  }

private:
  void negotiate() {
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s, [this] {
            return (connected_ && datagram_ready_ && reliable_reset_ready_) ||
                   closed_ || callback_failure_.load(
                                  std::memory_order_acquire) != STATUS_SUCCESS;
          }) ||
          !connected_ || !datagram_ready_ || !reliable_reset_ready_)
        throw std::runtime_error(
            "WebTransport transport negotiation timed out");
    }
    session_->set_negotiated_transport({true, true});
    if (!session_->send_local_settings(false).is_ok())
      throw std::runtime_error("cannot send WebTransport SETTINGS");
    {
      std::unique_lock lock(lock_);
      if (!changed_.wait_for(lock, 10s, [this] {
            return peer_settings_ready_ || closed_ ||
                   callback_failure_.load(std::memory_order_acquire) !=
                       STATUS_SUCCESS;
          }) ||
          !peer_settings_ready_ || !peer_settings_.server_ready())
        throw std::runtime_error("server WebTransport SETTINGS timed out");
    }
  }

  ntl::status fail_callback(NTSTATUS status) noexcept {
    callback_failure_.store(status, std::memory_order_release);
    changed_.notify_all();
    return status;
  }

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
    } catch (const std::bad_alloc &) {
      return fail_callback(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return fail_callback(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  backend_connection *connection_ = nullptr;
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
  bool response_received_ = false;
  unsigned response_status_ = 0;
  bool accepted_ = false;
  bool connect_stream_rejected_ = false;
  bool closed_ = false;
  std::atomic<NTSTATUS> callback_failure_{STATUS_SUCCESS};
};

class negative_http3_sink final
    : public ntl::net::quic::backend_sink,
      public ntl::net::http3::inspection_sink {
public:
  negative_http3_sink() noexcept
      : inspector_(qpack_,
                   {.maximum_concurrent_request_streams = 1,
                    .maximum_buffered_bytes_per_stream = 16 * 1024,
                    .frames = {16 * 1024}},
                   16 * 1024) {}

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
      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }

  ntl::status on_peer_unidirectional_stream(
      std::uint64_t, ntl::net::scatter_view, bool) noexcept override {
    // The server control stream is intentionally irrelevant to these raw
    // request-validation cases. Keeping it accepted isolates the result to
    // the request stream under test.
    return ntl::status::ok();
  }

  ntl::status on_peer_send_aborted(
      std::uint64_t stream_id, std::uint64_t) noexcept override {
    {
      std::lock_guard guard(lock_);
      if (stream_id == request_stream_id_)
        stream_rejected_ = true;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_peer_receive_aborted(
      std::uint64_t stream_id, std::uint64_t) noexcept override {
    return on_peer_send_aborted(stream_id, 0);
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
    bool seen = false;
    for (const auto &field : fields) {
      if (field.name != ":status")
        continue;
      if (seen)
        return STATUS_DATA_ERROR;
      const auto parsed = std::from_chars(
          field.value.data(), field.value.data() + field.value.size(), status);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != field.value.data() + field.value.size())
        return STATUS_DATA_ERROR;
      seen = true;
    }
    if (!seen)
      return STATUS_DATA_ERROR;
    {
      std::lock_guard guard(lock_);
      if (stream_id != request_stream_id_)
        return STATUS_DATA_ERROR;
      response_rejected_ = status >= 300 && status <= 599;
    }
    changed_.notify_all();
    return ntl::status::ok();
  }

  ntl::status on_data(std::uint64_t,
                      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }

  ntl::status on_stream_end(std::uint64_t) noexcept override {
    changed_.notify_all();
    return ntl::status::ok();
  }

  void wait_connected() {
    std::unique_lock lock(lock_);
    if (!changed_.wait_for(lock, 10s,
                           [this] { return connected_ || closed_; }) ||
        !connected_)
      throw std::runtime_error(
          "negative HTTP/3 client handshake timed out");
  }

  bool send_and_require_rejection(
      std::span<const ntl::net::http3::header_field> fields,
      std::string_view uppercase_patch = {}, bool append_data = false) {
    if (!connection_)
      throw std::logic_error("negative HTTP/3 client is not attached");

    std::uint64_t control = 0;
    auto settings =
        ntl::net::http3::webtransport::encode_control_stream(false);
    if (!settings ||
        !connection_->open_unidirectional_stream(control).is_ok() ||
        !connection_->write_stream(
                         control,
                         ntl::net::scatter_view::from_contiguous(*settings),
                         false)
             .is_ok())
      throw std::runtime_error(
          "cannot send negative HTTP/3 client SETTINGS");

    ntl::net::http3::bounded_static_qpack_encoder encoder;
    auto header_block = encoder.encode(fields, 16 * 1024);
    if (!header_block)
      throw std::runtime_error("cannot encode negative HTTP/3 request");
    if (!uppercase_patch.empty()) {
      const auto bytes = std::as_bytes(std::span(uppercase_patch));
      const auto found = std::search(header_block->begin(),
                                     header_block->end(), bytes.begin(),
                                     bytes.end());
      if (found == header_block->end())
        throw std::runtime_error(
            "cannot locate HTTP/3 header name mutation point");
      *found = std::byte{static_cast<unsigned char>('X')};
    }

    std::vector<std::byte> wire;
    if (!ntl::net::http3::append_quic_varint(
             wire,
             static_cast<std::uint64_t>(ntl::net::http3::frame_type::headers))
             .is_ok() ||
        !ntl::net::http3::append_quic_varint(wire, header_block->size())
             .is_ok())
      throw std::runtime_error("cannot frame negative HTTP/3 HEADERS");
    wire.insert(wire.end(), header_block->begin(), header_block->end());
    if (append_data) {
      constexpr std::array<std::byte, 3> payload{
          std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
      if (!ntl::net::http3::append_quic_varint(
               wire,
               static_cast<std::uint64_t>(ntl::net::http3::frame_type::data))
               .is_ok() ||
          !ntl::net::http3::append_quic_varint(wire, payload.size()).is_ok())
        throw std::runtime_error("cannot frame negative HTTP/3 DATA");
      wire.insert(wire.end(), payload.begin(), payload.end());
    }

    std::uint64_t request = 0;
    if (!connection_->open_request_stream(request).is_ok())
      throw std::runtime_error("cannot open negative HTTP/3 request stream");
    {
      std::lock_guard guard(lock_);
      request_stream_id_ = request;
    }
    // Deliberately no FIN: rejection must happen at HEADERS, independent of
    // request completion. One unsupported CONNECT case also carries DATA.
    if (!connection_->write_stream(
                         request,
                         ntl::net::scatter_view::from_contiguous(wire), false)
             .is_ok())
      throw std::runtime_error("cannot send negative HTTP/3 request");

    std::unique_lock lock(lock_);
    if (!changed_.wait_for(lock, 2s, [this] {
          return response_rejected_ || stream_rejected_ || closed_;
        }))
      throw std::runtime_error(
          "HTTP/3 request was not rejected at the header stage");
    return response_rejected_ || stream_rejected_;
  }

private:
  backend_connection *connection_ = nullptr;
  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      qpack_;
  ntl::net::http3::borrowed_connection_inspector inspector_;
  std::mutex lock_;
  std::condition_variable changed_;
  std::uint64_t request_stream_id_ =
      (std::numeric_limits<std::uint64_t>::max)();
  bool connected_ = false;
  bool response_rejected_ = false;
  bool stream_rejected_ = false;
  bool closed_ = false;
};

bool exercise_raw_http3_rejection(
    int address_family, std::uint16_t port,
    std::span<const ntl::net::http3::header_field> fields,
    std::string_view uppercase_patch = {}, bool append_data = false) {
  client_configuration configuration;
  auto sink = std::make_shared<negative_http3_sink>();
  auto connected = backend_connection::try_connect(
      configuration.connection_context(),
      controlled_wfp_target(address_family, port), sink,
      {.maximum_streams = 8,
       .maximum_receive_indication = 32 * 1024,
       .maximum_send_size = 32 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error(
        "cannot create negative HTTP/3 client connection");
  auto connection = std::move(*connected);
  sink->attach(*connection);
  sink->wait_connected();
  const bool rejected = sink->send_and_require_rejection(
      fields, uppercase_patch, append_data);
  connection->stop();
  // A peer stream abort is the expected result. drain() still performs the
  // full callback/lifetime barrier even when it reports that terminal status.
  (void)connection->drain();
  return rejected;
}

} // namespace

response exchange_http3_request_to(
    backend_target target,
    std::string_view method, std::string_view path, bool block,
    bool dynamic_qpack, std::string_view authority,
    std::string_view content_type,
    std::span<const std::byte> body,
    std::span<const std::byte> client_certificate_thumbprint = {}) {
  client_configuration configuration(client_certificate_thumbprint);
  auto sink = std::make_shared<client_sink>();
  auto connected = backend_connection::try_connect(
      configuration.connection_context(), std::move(target), sink,
      {.maximum_streams = 32,
       .maximum_receive_indication = 128 * 1024,
       .maximum_send_size = 128 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error("cannot create HTTP/3 client connection");
  auto connection = std::move(*connected);
  sink->attach(*connection);
  sink->wait_connected();
  sink->send_request(method, path, block, dynamic_qpack, authority,
                     content_type, body);
  auto result = sink->wait_response();
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

response exchange_http3_request(
    int address_family, std::uint16_t port,
    std::string_view method, std::string_view path, bool block,
    bool dynamic_qpack, std::string_view authority,
    std::string_view content_type,
    std::span<const std::byte> body) {
  return exchange_http3_request_to(
      controlled_wfp_target(address_family, port), method, path, block,
      dynamic_qpack, authority, content_type, body);
}

response exchange_http3(int address_family, std::uint16_t port,
                        std::string_view path, bool block,
                        bool dynamic_qpack,
                        std::string_view authority) {
  return exchange_http3_request(
      address_family, port, "GET", path, block, dynamic_qpack, authority,
      {}, {});
}

void require_http3_origin_handshake_direct(
    int address_family, std::uint16_t port,
    std::span<const std::byte> client_certificate_thumbprint) {
  client_configuration configuration(client_certificate_thumbprint);
  auto sink = std::make_shared<client_sink>();
  auto connected = backend_connection::try_connect(
      configuration.connection_context(),
      direct_loopback_target(address_family, port), sink,
      {.maximum_streams = 4,
       .maximum_receive_indication = 16 * 1024,
       .maximum_send_size = 16 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error(
        "cannot create direct HTTP/3 origin connection");
  auto connection = std::move(*connected);
  sink->attach(*connection);
  sink->wait_connected();
  connection->stop();
  if (!connection->drain().is_ok())
    throw std::runtime_error(
        "direct HTTP/3 origin connection did not drain");
}

response exchange_http3_grpc(int address_family, std::uint16_t port,
                             std::string_view authority) {
  constexpr std::string_view fixture = "ntl-grpc-transform";
  const auto encoded = ntl::net::grpc::encode_message(
      std::as_bytes(std::span(fixture)), false, 4096);
  if (!encoded)
    throw std::runtime_error("cannot encode managed HTTP/3 gRPC fixture");
  return exchange_http3_request(
      address_family, port, "POST", "/grpc", false, true, authority,
      "application/grpc", *encoded);
}

http3_multiplex_result exercise_http3_multiplex(
    int address_family, std::uint16_t port, std::string_view authority) {
  client_configuration configuration;
  auto sink = std::make_shared<multiplex_client_sink>();
  auto connected = backend_connection::try_connect(
      configuration.connection_context(),
      controlled_wfp_target(address_family, port), sink,
      {.maximum_streams = 64,
       .maximum_receive_indication = 128 * 1024,
       .maximum_send_size = 128 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error("cannot create multiplex HTTP/3 connection");
  auto connection = std::move(*connected);
  sink->attach(*connection);
  sink->wait_connected();
  std::cout << "[kernel-browser][h3][multiplex] connected" << std::endl;
  sink->send_requests(authority);
  std::cout << "[kernel-browser][h3][multiplex] requests-sent" << std::endl;
  auto result = sink->wait_result();
  std::cout << "[kernel-browser][h3][multiplex] responses-complete"
            << std::endl;
  result.aggregate_quota = sink->exercise_aggregate_quota(authority);
  std::cout << "[kernel-browser][h3][multiplex] quota-complete"
            << std::endl;
  connection->stop();
  std::cout << "[kernel-browser][h3][multiplex] stop-issued" << std::endl;
  result.clean_drain = connection->drain().is_ok();
  std::cout << "[kernel-browser][h3][multiplex] drain-complete="
            << result.clean_drain << std::endl;
  if (!result.clean_drain)
    throw std::runtime_error("multiplex HTTP/3 shutdown timed out");
  return result;
}

webtransport_result exercise_webtransport(int address_family,
                                           std::uint16_t port,
                                           std::string_view authority) {
  client_configuration configuration;
  auto sink = std::make_shared<webtransport_client_sink>();
  auto connected = backend_connection::try_connect(
      configuration.connection_context(),
      controlled_wfp_target(address_family, port), sink,
      {.maximum_streams = 64,
       .maximum_receive_indication = 128 * 1024,
       .maximum_send_size = 128 * 1024,
       .maximum_prefix_bytes = 8,
       .shutdown_timeout = 10s});
  if (!connected)
    throw std::runtime_error("cannot create WebTransport client connection");
  std::shared_ptr<backend_connection> connection = std::move(*connected);
  sink->attach(connection);
  auto result = sink->run(authority);
  connection->stop();
  if (!connection->drain().is_ok())
    throw std::runtime_error("WebTransport client shutdown timed out");
  return result;
}

http3_negative_acceptance_result
exercise_http3_negative_acceptance(int address_family, std::uint16_t port,
                                   std::string_view authority) {
  http3_negative_acceptance_result result{};
  {
    client_configuration configuration;
    auto sink = std::make_shared<webtransport_client_sink>();
    auto connected = backend_connection::try_connect(
        configuration.connection_context(),
        controlled_wfp_target(address_family, port), sink,
        {.maximum_streams = 16,
         .maximum_receive_indication = 32 * 1024,
         .maximum_send_size = 32 * 1024,
         .maximum_prefix_bytes = 8,
         .shutdown_timeout = 10s});
    if (!connected)
      throw std::runtime_error(
          "cannot create blocked WebTransport client connection");
    std::shared_ptr<backend_connection> connection = std::move(*connected);
    sink->attach(connection);
    const auto blocked = sink->run_policy_rejection();
    result.webtransport_policy_rejected = blocked.rejected;
    result.webtransport_client_inactive = blocked.client_inactive;
    connection->stop();
    if (!connection->drain().is_ok())
      throw std::runtime_error(
          "blocked WebTransport client shutdown timed out");
  }

  const auto extended_connect = [&authority](std::string protocol) {
    return std::vector<ntl::net::http3::header_field>{
        {":method", "CONNECT"},
        {":protocol", std::move(protocol)},
        {":scheme", "https"},
        {":authority", std::string(authority)},
        {":path", "/unsupported-connect"}};
  };
  const auto ordinary = [&authority](ntl::net::http3::header_field probe) {
    return std::vector<ntl::net::http3::header_field>{
        {":method", "GET"},
        {":scheme", "https"},
        {":authority", std::string(authority)},
        {":path", "/invalid-header"},
        std::move(probe)};
  };

  const auto websocket = extended_connect("websocket");
  result.websocket_extended_connect_rejected = exercise_raw_http3_rejection(
      address_family, port, websocket);
  const auto unknown = extended_connect("ntl-unknown");
  result.unknown_extended_connect_rejected = exercise_raw_http3_rejection(
      address_family, port, unknown, {}, true);
  constexpr std::string_view uppercase_probe = "x-ntl-uppercase";
  const auto uppercase = ordinary({std::string(uppercase_probe), "1"});
  result.uppercase_header_rejected = exercise_raw_http3_rejection(
      address_family, port, uppercase, uppercase_probe);
  const auto connection = ordinary({"connection", "keep-alive"});
  result.connection_header_rejected = exercise_raw_http3_rejection(
      address_family, port, connection);
  const auto bad_te = ordinary({"te", "gzip"});
  result.bad_te_header_rejected = exercise_raw_http3_rejection(
      address_family, port, bad_te);
  return result;
}

} // namespace crtsys::wfp_kernel_browser_https
