#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>

#include "http3_live_proxy.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

#include <msh3.h>

#include <ntl/net/http/transform>
#include <ntl/net/http3/msh3_backend>
#include <ntl/net/http3/standard_inspection_proxy>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/inspection_frontend>
#include <ntl/net/tls/inspection_policy>

#include "browser_log.hpp"
#include "browser_policy.hpp"
#include "http1_support.hpp"
#include "http3_origin.hpp"
#include "test_certificate.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

constexpr std::size_t maximum_dynamic_hosts = 256;

std::wstring widen_dns_name(std::string_view value) {
  std::wstring result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    if (character == 0 || character > 0x7f)
      throw std::invalid_argument(
          "HTTP/3 SNI is not an ASCII DNS name");
    result.push_back(static_cast<wchar_t>(character));
  }
  return result;
}

void export_certificate(
    PCCERT_CONTEXT certificate,
    const std::filesystem::path &path) {
  if (!certificate)
    throw std::invalid_argument(
        "cannot export an empty certificate");
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error(
        "cannot create HTTP/3 certificate file");
  output.write(
      reinterpret_cast<const char *>(
          certificate->pbCertEncoded),
      static_cast<std::streamsize>(
          certificate->cbCertEncoded));
  if (!output)
    throw std::runtime_error(
        "cannot write HTTP/3 certificate file");
}

std::string certificate_spki_sha256(
    PCCERT_CONTEXT certificate) {
  if (!certificate || !certificate->pCertInfo)
    throw std::invalid_argument(
        "cannot hash an empty certificate");
  std::array<BYTE, 32> hash{};
  DWORD hash_size = static_cast<DWORD>(hash.size());
  if (!::CryptHashPublicKeyInfo(
          0, CALG_SHA_256, 0, X509_ASN_ENCODING,
          &certificate->pCertInfo->SubjectPublicKeyInfo,
          hash.data(), &hash_size))
    throw_windows("CryptHashPublicKeyInfo(SPKI)");
  DWORD text_size = 0;
  if (!::CryptBinaryToStringA(
          hash.data(), hash_size,
          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
          nullptr, &text_size))
    throw_windows("CryptBinaryToStringA(SPKI size)");
  std::string result(text_size, '\0');
  if (!::CryptBinaryToStringA(
          hash.data(), hash_size,
          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
          result.data(), &text_size))
    throw_windows("CryptBinaryToStringA(SPKI)");
  result.resize(text_size);
  while (!result.empty() && result.back() == '\0')
    result.pop_back();
  return result;
}

struct http3_identity {
  ntl::net::issued_tls_certificate certificate;
  MSH3_CONFIGURATION *configuration = nullptr;

  http3_identity(
      ntl::net::issued_tls_certificate issued,
      MSH3_CONFIGURATION *configured) noexcept
      : certificate(std::move(issued)),
        configuration(configured) {}

  http3_identity(const http3_identity &) = delete;
  http3_identity &operator=(const http3_identity &) = delete;

  ~http3_identity() {
    if (configuration)
      ::MsH3ConfigurationClose(configuration);
  }
};

class dynamic_http3_identity_provider final
    : public ntl::net::http3::msh3_backend::
          server_identity_provider {
public:
  dynamic_http3_identity_provider(
      MSH3_API *api,
      MSH3_SETTINGS settings,
      ntl::net::windows_tls_certificate_issuer &issuer,
      browser_html_logger &logger) noexcept
      : api_(api),
        settings_(settings),
        issuer_(&issuer),
        logger_(&logger) {}

  ntl::result<MSH3_CONFIGURATION *>
  configuration_for(
      std::string_view server_name) noexcept override {
    try {
      std::wstring normalized =
          widen_dns_name(server_name);
      for (wchar_t &character : normalized) {
        if (character >= L'A' && character <= L'Z')
          character = static_cast<wchar_t>(
              character - L'A' + L'a');
      }
      if (normalized.empty() ||
          normalized.find_first_of(L"/\\:") !=
              std::wstring::npos)
        return ntl::unexpected(STATUS_INVALID_PARAMETER);

      std::lock_guard guard(lock_);
      if (const auto found = identities_.find(normalized);
          found != identities_.end())
        return ntl::ok(found->second->configuration);
      if (identities_.size() >= maximum_dynamic_hosts)
        return ntl::unexpected(STATUS_QUOTA_EXCEEDED);

      auto certificate = issuer_->issue(normalized);
      MSH3_CONFIGURATION *configuration =
          ::MsH3ConfigurationOpen(
              api_, &settings_, sizeof(settings_));
      if (!configuration)
        return ntl::unexpected(STATUS_UNSUCCESSFUL);
      struct configuration_cleanup {
        MSH3_CONFIGURATION *value;
        ~configuration_cleanup() {
          if (value)
            ::MsH3ConfigurationClose(value);
        }
      } cleanup{configuration};

      MSH3_CREDENTIAL_CONFIG credentials{};
      credentials.Type =
          MSH3_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
      credentials.Flags = MSH3_CREDENTIAL_FLAG_NONE;
      credentials.CertificateContext =
          reinterpret_cast<MSH3_CERTIFICATE_CONTEXT *>(
              const_cast<CERT_CONTEXT *>(
                  certificate.get()));
      const MSH3_STATUS loaded =
          ::MsH3ConfigurationLoadCredential(
              configuration, &credentials);
      if (MSH3_FAILED(loaded))
        return ntl::unexpected(
            static_cast<NTSTATUS>(loaded));
      logger_->record_lifecycle(
          "http3-credential host=" +
          narrow_dns_name(normalized) +
          " status=" +
          std::to_string(
              static_cast<unsigned long>(loaded)));

      auto inserted = identities_.emplace(
          normalized,
          std::make_unique<http3_identity>(
              std::move(certificate), configuration));
      cleanup.value = nullptr;
      logger_->record_lifecycle(
          "http3-identity host=" +
          narrow_dns_name(normalized));
      return ntl::ok(
          inserted.first->second->configuration);
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(
          STATUS_INSUFFICIENT_RESOURCES);
    } catch (const std::exception &error) {
      logger_->record_error(error.what());
      return ntl::unexpected(STATUS_UNSUCCESSFUL);
    } catch (...) {
      logger_->record_error(
          "unknown HTTP/3 identity selection failure");
      return ntl::unexpected(
          STATUS_UNHANDLED_EXCEPTION);
    }
  }

  PCCERT_CONTEXT certificate_for(
      std::string_view server_name) {
    auto configured = configuration_for(server_name);
    if (!configured)
      throw std::system_error(
          static_cast<int>(
              static_cast<NTSTATUS>(configured.status())),
          std::system_category(),
          "HTTP/3 identity configuration");
    std::wstring normalized = widen_dns_name(server_name);
    for (wchar_t &character : normalized) {
      if (character >= L'A' && character <= L'Z')
        character = static_cast<wchar_t>(
            character - L'A' + L'a');
    }
    std::lock_guard guard(lock_);
    return identities_.at(normalized)->certificate.get();
  }

  std::size_t size() const noexcept {
    std::lock_guard guard(lock_);
    return identities_.size();
  }

private:
  MSH3_API *api_ = nullptr;
  MSH3_SETTINGS settings_{};
  ntl::net::windows_tls_certificate_issuer *issuer_ =
      nullptr;
  browser_html_logger *logger_ = nullptr;
  mutable std::mutex lock_;
  std::unordered_map<
      std::wstring, std::unique_ptr<http3_identity>>
      identities_;
};

class winhttp_http3_origin_transport final
    : public ntl::net::http3::origin_transport {
public:
  winhttp_http3_origin_transport(
      browser_html_logger &logger,
      ntl::net::inspection::origin_client_identity_provider
          &origin_identities,
      http3_origin_policy origin_policy) noexcept
      : logger_(&logger),
        origin_identities_(&origin_identities),
        origin_policy_(origin_policy) {}

  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request)
      noexcept override {
    try {
      http3_header_fields headers;
      headers.reserve(request.headers.size());
      for (const auto &field : request.headers)
        headers.emplace_back(field.name, field.value);
      auto origin =
          origin_policy_ ==
                  http3_origin_policy::require_http3
              ? fetch_http3_origin_winhttp(
                    widen_dns_name(request.server_name),
                    request.method, request.path, headers,
                    request.body, *origin_identities_)
              : fetch_http_origin_with_transport_fallback_winhttp(
                    widen_dns_name(request.server_name),
                    request.method, request.path, headers,
                    request.body, *origin_identities_);

      ntl::net::http3::origin_response result;
      result.status = origin.message.status;
      result.headers.reserve(origin.headers.size());
      for (auto &[name, value] : origin.headers)
        result.headers.push_back(
            {std::move(name), std::move(value)});
      if (origin.headers.empty() &&
          !origin.message.content_type.empty())
        result.headers.push_back(
            {"content-type",
             origin.message.content_type});
      result.body = std::move(origin.message.body);
      result.negotiated_protocol =
          std::move(origin.negotiated_protocol);
      return ntl::ok(std::move(result));
    } catch (const std::bad_alloc &) {
      logger_->record_error(
          "HTTP/3 origin exhausted its bounded memory");
      return ntl::unexpected(
          STATUS_INSUFFICIENT_RESOURCES);
    } catch (const std::exception &error) {
      logger_->record_error(error.what());
      return ntl::unexpected(STATUS_UNSUCCESSFUL);
    } catch (...) {
      logger_->record_error(
          "unknown HTTP/3 origin request failure");
      return ntl::unexpected(
          STATUS_UNHANDLED_EXCEPTION);
    }
  }

private:
  browser_html_logger *logger_ = nullptr;
  ntl::net::inspection::origin_client_identity_provider
      *origin_identities_ = nullptr;
  http3_origin_policy origin_policy_ =
      http3_origin_policy::require_http3;
};

class browser_http3_inspection_policy final
    : public ntl::net::http3::inspection_policy {
public:
  explicit browser_http3_inspection_policy(
      browser_html_logger &logger) noexcept
      : logger_(&logger) {}

  ntl::net::inspection::verdict inspect_request(
      const ntl::net::http3::request_view &request)
      noexcept override {
    try {
      logger_->record_lifecycle(
          "http3-request host=" +
          request.message.server_name +
          " method=" + request.message.method +
          " path=" + request.message.path);
      return ntl::net::inspection::verdict::permit;
    } catch (...) {
      return ntl::net::inspection::verdict::block;
    }
  }

  ntl::net::inspection::verdict inspect_response(
      const ntl::net::http3::response_view &response)
      noexcept override {
    try {
      const auto host =
          widen_dns_name(response.request.server_name);
      logger_->record_protocol(host, "h3");
      if (response.message.negotiated_protocol != "h3")
        logger_->record_lifecycle(
            "http3-origin-fallback host=" +
            response.request.server_name +
            " upstream=" +
            response.message.negotiated_protocol);

      parsed_http_response inspected;
      inspected.status = response.message.status;
      inspected.content_type.assign(response.content_type);
      inspected.content_encoding.assign(
          response.content_encoding);
      inspected.body.assign(
          response.decoded_body.begin(),
          response.decoded_body.end());
      inspected.wire_size =
          response.message.body.size();
      inspected.body_decoded = true;
      for (const auto &field : response.message.headers) {
        if (field.name == "location")
          inspected.location = field.value;
      }
      const auto html =
          logger_->record_response(host, inspected);
      std::wosyncstream(std::wcout)
          << L"NTL HTTP/3 inspected: host=" << host
          << L", downstream=h3, upstream="
          << widen_dns_name(
                 response.message.negotiated_protocol)
          << L", status=" << response.message.status
          << L", html="
          << (html ? html->wstring()
                   : std::wstring(L"none"))
          << L'\n';
      return ntl::net::inspection::verdict::permit;
    } catch (const std::exception &error) {
      logger_->record_error(error.what());
      return ntl::net::inspection::verdict::block;
    } catch (...) {
      logger_->record_error(
          "unknown HTTP/3 inspection policy failure");
      return ntl::net::inspection::verdict::block;
    }
  }

  void on_failure(NTSTATUS status) noexcept override {
    try {
      logger_->record_error(
          "HTTP/3 inspection status=" +
          std::to_string(
              static_cast<unsigned long>(status)));
    } catch (...) {
    }
  }

  browser_html_logger *logger_ = nullptr;
};

class browser_http3_request_handler final
    : public ntl::net::http3::msh3_backend::request_handler {
public:
  browser_http3_request_handler(
      browser_html_logger &logger,
      ntl::net::http3::origin_transport &origin,
      bool require_http3_origin,
      const ntl::net::http::transform_pipeline *transforms)
      : logger_(&logger),
        policy_(logger),
        proxy_(
            origin, policy_, transforms,
            {.require_http3_origin =
                 require_http3_origin}) {}

  ntl::result<ntl::net::http3::msh3_backend::response>
  handle(
      const ntl::net::http3::msh3_backend::request
          &request) noexcept override {
    try {
      ntl::net::http3::incoming_request incoming;
      incoming.server_name = request.server_name;
      incoming.headers.reserve(request.headers.size());
      for (const auto &field : request.headers)
        incoming.headers.push_back(
            {field.name, field.value});
      incoming.body = request.body;

      auto forwarded = proxy_.forward(std::move(incoming));
      if (!forwarded)
        return ntl::unexpected(forwarded.status());
      ntl::net::http3::msh3_backend::response result;
      result.status = forwarded->status;
      result.headers.reserve(forwarded->headers.size());
      for (auto &field : forwarded->headers)
        result.headers.push_back(
            {std::move(field.name),
             std::move(field.value)});
      result.body = std::move(forwarded->body);
      return ntl::ok(std::move(result));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(
          STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(
          STATUS_UNHANDLED_EXCEPTION);
    }
  }

  void on_transport_error(
      NTSTATUS status) noexcept override {
    logger_->record_error(
        "HTTP/3 transport status=" +
        std::to_string(
            static_cast<unsigned long>(status)));
  }

private:
  browser_html_logger *logger_ = nullptr;
  browser_http3_inspection_policy policy_;
  ntl::net::http3::standard_inspection_proxy proxy_;
};

} // namespace

class browser_http3_service::implementation {
public:
  implementation(
      ntl::net::windows_tls_certificate_issuer &issuer,
      browser_html_logger &logger,
      std::uint16_t listen_port,
      ntl::net::inspection::origin_client_identity_provider
          *origin_identities,
      http3_origin_policy origin_policy,
      ntl::net::http3::origin_transport *origin_transport,
      const ntl::net::http::transform_pipeline *transforms)
      : api_(),
        settings_(make_settings(origin_policy)),
        identities_(
            api_.value, settings_, issuer, logger),
        default_origin_identity_(),
        default_origin_transport_(
            logger,
            origin_identities
                ? *origin_identities
                : static_cast<
                      ntl::net::inspection::
                          origin_client_identity_provider &>(
                      default_origin_identity_),
            origin_policy),
        default_transforms_(make_default_transforms()),
        handler_(
            logger,
            origin_transport
                ? *origin_transport
                : static_cast<
                      ntl::net::http3::origin_transport &>(
                      default_origin_transport_),
            origin_policy ==
                http3_origin_policy::require_http3,
            transforms ? transforms : &default_transforms_),
        backend_v4_(
            api_.value, identities_, handler_,
            backend_limits()),
        backend_v6_(
            api_.value, identities_, handler_,
            backend_limits()),
        port_(listen_port) {
    if (port_ == 0)
      throw std::invalid_argument(
          "HTTP/3 service requires a nonzero listen port");

    MSH3_ADDR address_v4{};
    address_v4.Ipv4.sin_family = AF_INET;
    address_v4.Ipv4.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);
    MSH3_SET_PORT(&address_v4, port_);
    const ntl::status started_v4 =
        backend_v4_.start(address_v4);
    if (!started_v4.is_ok())
      throw_status(started_v4, "IPv4");

    MSH3_ADDR address_v6{};
    address_v6.Ipv6.sin6_family = AF_INET6;
    address_v6.Ipv6.sin6_addr = in6addr_loopback;
    MSH3_SET_PORT(&address_v6, port_);
    const ntl::status started_v6 =
        backend_v6_.start(address_v6);
    if (!started_v6.is_ok()) {
      backend_v4_.stop();
      (void)backend_v4_.wait_for_drain(
          std::chrono::seconds(15));
      throw_status(started_v6, "IPv6");
    }
  }

  void stop() noexcept {
    backend_v4_.stop();
    backend_v6_.stop();
  }

  bool wait_for_drain(
      std::uint32_t seconds) noexcept {
    const auto timeout = std::chrono::seconds(seconds);
    return backend_v4_.wait_for_drain(timeout) &&
           backend_v6_.wait_for_drain(timeout);
  }

  std::size_t delivered_requests() const noexcept {
    return backend_v4_.delivered_requests() +
           backend_v6_.delivered_requests();
  }

  std::string certificate_spki(
      std::string_view server_name) {
    return certificate_spki_sha256(
        identities_.certificate_for(server_name));
  }

  std::size_t dynamic_hosts() const noexcept {
    return identities_.size();
  }

  std::uint16_t port() const noexcept { return port_; }

private:
  struct api_owner {
    api_owner() : value(::MsH3ApiOpen()) {
      if (!value)
        throw std::runtime_error("MsH3ApiOpen failed");
    }
    ~api_owner() {
      if (value)
        ::MsH3ApiClose(value);
    }
    MSH3_API *value = nullptr;
  };

  static MSH3_SETTINGS make_settings(
      http3_origin_policy origin_policy) noexcept {
    MSH3_SETTINGS settings{};
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs =
        origin_policy ==
                http3_origin_policy::allow_tls_tcp_fallback
            ? 90000
            : 30000;
    settings.IsSet.PeerRequestCount = 1;
    settings.PeerRequestCount = 64;
    return settings;
  }

  static ntl::net::http3::msh3_backend::server_limits
  backend_limits() noexcept {
    return {
        .maximum_request_headers = 128,
        .maximum_request_header_bytes = 32 * 1024,
        .maximum_request_body_bytes = 2 * 1024 * 1024,
        .maximum_response_headers = 256,
        .maximum_response_header_bytes = 48 * 1024,
        .maximum_response_body_bytes = 4 * 1024 * 1024};
  }

  static ntl::net::http::transform_pipeline
  make_default_transforms() {
    ntl::net::http::transform_pipeline transforms;
    transforms.requests().transform(
        [](ntl::net::http::request_message &request) {
          request.headers.erase("proxy-connection");
          return ntl::net::http::rewrite_result::headers_changed();
        });
    return transforms;
  }

  [[noreturn]] static void throw_status(
      ntl::status status,
      const char *family) {
    throw std::system_error(
        static_cast<int>(
            static_cast<NTSTATUS>(status)),
        std::system_category(),
        std::string("msh3 HTTP/3 ") + family +
            " listener");
  }

  api_owner api_;
  MSH3_SETTINGS settings_{};
  dynamic_http3_identity_provider identities_;
  ntl::net::inspection::unavailable_origin_client_identity
      default_origin_identity_;
  winhttp_http3_origin_transport
      default_origin_transport_;
  ntl::net::http::transform_pipeline default_transforms_;
  browser_http3_request_handler handler_;
  ntl::net::http3::msh3_backend::server backend_v4_;
  ntl::net::http3::msh3_backend::server backend_v6_;
  std::uint16_t port_ = 0;
};

browser_http3_service::browser_http3_service(
    ntl::net::windows_tls_certificate_issuer &issuer,
    browser_html_logger &logger,
    std::uint16_t listen_port,
    ntl::net::inspection::origin_client_identity_provider
        *origin_identities,
    http3_origin_policy origin_policy,
    ntl::net::http3::origin_transport *origin_transport,
    const ntl::net::http::transform_pipeline *transforms)
    : implementation_(
          std::make_unique<implementation>(
              issuer, logger, listen_port,
              origin_identities, origin_policy,
              origin_transport, transforms)) {}

browser_http3_service::~browser_http3_service() = default;

void browser_http3_service::stop() noexcept {
  implementation_->stop();
}

bool browser_http3_service::wait_for_drain(
    std::uint32_t timeout_seconds) noexcept {
  return implementation_->wait_for_drain(timeout_seconds);
}

std::uint16_t browser_http3_service::port() const noexcept {
  return implementation_->port();
}

std::size_t
browser_http3_service::delivered_requests() const noexcept {
  return implementation_->delivered_requests();
}

std::size_t
browser_http3_service::dynamic_hosts() const noexcept {
  return implementation_->dynamic_hosts();
}

std::string browser_http3_service::certificate_spki(
    std::string_view server_name) {
  return implementation_->certificate_spki(server_name);
}

int run_managed_http3_proxy(
    std::uint16_t listen_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  if (listen_port == 0)
    throw std::invalid_argument(
        "managed HTTP/3 proxy requires a nonzero port");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  browser_html_logger logger(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate_authority;
  const auto ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate_authority.export_public_certificate(ca_path);
  ntl::net::windows_tls_certificate_issuer issuer(
      certificate_authority.get(),
      {.key_name_prefix =
           L"crtsys-ntl-managed-http3",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .reuse_leaf_key = false});
  browser_http3_service service(
      issuer, logger, listen_port, nullptr,
      http3_origin_policy::allow_tls_tcp_fallback);

  std::wcout
      << L"NTL managed HTTP/3 inspection ready: "
      << L"listen=127.0.0.1:" << listen_port
      << L", ca=" << ca_path.wstring()
      << L", logs=" << log_directory.wstring()
      << L", browser-settings=unchanged, wfp-udp=not-used\n";

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error, "query managed HTTP/3 proxy stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  service.stop();
  if (!service.wait_for_drain(15))
    throw std::runtime_error(
        "managed HTTP/3 connections did not drain");
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }
  std::wcout
      << L"NTL managed HTTP/3 inspection stopped: "
      << L"delivered-requests="
      << service.delivered_requests()
      << L", html-files=" << logger.html_files()
      << L", dynamic-hosts=" << service.dynamic_hosts()
      << L", downstream=h3, upstream=recorded-per-request"
      << L", persistent-browser-changes=none\n";
  return 0;
}

int run_wfp_managed_http3_proxy(
    const std::filesystem::path &client_argument,
    std::uint16_t listen_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  if (listen_port == 0)
    throw std::invalid_argument(
        "WFP managed HTTP/3 proxy requires a nonzero port");
  const auto client =
      std::filesystem::canonical(client_argument);
  if (!std::filesystem::is_regular_file(client))
    throw std::invalid_argument(
        "managed HTTP/3 client path is not a regular file");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  browser_html_logger logger(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate_authority;
  const auto ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate_authority.export_public_certificate(ca_path);
  ntl::net::windows_tls_certificate_issuer issuer(
      certificate_authority.get(),
      {.key_name_prefix =
           L"crtsys-ntl-wfp-managed-http3",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = true,
       .reuse_leaf_key = false});
  browser_http3_service service(
      issuer, logger, listen_port, nullptr,
      http3_origin_policy::allow_tls_tcp_fallback);

  const auto client_id =
      ntl::wfp::application_id::from_path(client.wstring());
  std::optional<ntl::wfp::policy_session> policy;
  policy.emplace(ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp managed HTTP/3 redirect"));
  install_managed_http3_redirect_policy(
      *policy, client_id, listen_port);

  std::wcout
      << L"NTL WFP managed HTTP/3 inspection ready: client="
      << client.wstring() << L", udp443=redirected, listen="
      << listen_port << L", ca=" << ca_path.wstring()
      << L", logs=" << log_directory.wstring() << L'\n';

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error, "query WFP managed HTTP/3 stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  policy.reset();
  service.stop();
  if (!service.wait_for_drain(15))
    throw std::runtime_error(
        "WFP managed HTTP/3 connections did not drain");
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }
  std::wcout
      << L"NTL WFP managed HTTP/3 inspection stopped: "
      << L"delivered-requests=" << service.delivered_requests()
      << L", html-files=" << logger.html_files()
      << L", dynamic-hosts=" << service.dynamic_hosts()
      << L", wfp-policy=removed, persistent-trust=none\n";
  return 0;
}

int run_browser_http3_spki_proxy(
    std::wstring_view server_name,
    std::uint16_t listen_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  const std::wstring host(server_name);
  const std::string host_ascii = narrow_dns_name(host);
  if (host.empty() ||
      host_ascii.find_first_of("/\\:") !=
          std::string::npos)
    throw std::invalid_argument(
        "HTTP/3 SPKI diagnostic requires one DNS host name");
  if (listen_port == 0)
    throw std::invalid_argument(
        "HTTP/3 SPKI diagnostic requires a nonzero port");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  browser_html_logger logger(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }

  ephemeral_certificate certificate_authority;
  const auto ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  certificate_authority.export_public_certificate(ca_path);
  ntl::net::windows_tls_certificate_issuer issuer(
      certificate_authority.get(),
      {.key_name_prefix =
           L"crtsys-ntl-wfp-browser-http3",
       .rsa_bits = 2048,
       .validity_days = 2,
       .machine_keys = false,
       .reuse_leaf_key = true});
  auto leaf = issuer.issue(host);
  if (!leaf)
    throw std::runtime_error(
        "HTTP/3 diagnostic could not issue its leaf certificate");
  const auto leaf_path =
      log_directory / L"ntl-browser-http3-leaf.cer";
  export_certificate(leaf.get(), leaf_path);
  const std::string spki =
      certificate_spki_sha256(leaf.get());
  const auto spki_path =
      log_directory / L"ntl-browser-http3-spki.txt";
  {
    std::ofstream output(
        spki_path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error(
          "cannot create HTTP/3 SPKI file");
    output << spki << '\n';
  }

  ntl::net::inspection::tls_inspection_observation observation;
  observation.transport =
      ntl::net::inspection::encrypted_transport::quic;
  observation.protocol =
      ntl::net::inspection::application_protocol::http3;
  observation.server_name = host;
  observation.negotiated_alpn = "h3";
  observation.protocol_adapter_available = true;
  observation.content_decoder_available = true;
  observation.quic_backend_available = true;
  const ntl::net::inspection::explicit_tls_inspection_policy policy;
  if (policy.decide(observation).action !=
      ntl::net::inspection::tls_inspection_action::inspect)
    throw std::runtime_error(
        "HTTP/3 diagnostic was rejected by inspection policy");

  browser_http3_service service(
      issuer, logger, listen_port);
  if (service.certificate_spki(host_ascii) != spki)
    throw std::runtime_error(
        "HTTP/3 shared leaf key did not preserve its SPKI");

  std::wcout
      << L"NTL browser HTTP/3 diagnostic ready: "
      << L"initial-host=" << host
      << L", listen=127.0.0.1:" << listen_port
      << L", ca=" << ca_path.wstring()
      << L", leaf=" << leaf_path.wstring()
      << L", spki-file=" << spki_path.wstring()
      << L", logs=" << log_directory.wstring() << L'\n';

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error, "query HTTP/3 proxy stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  service.stop();
  if (!service.wait_for_drain(15))
    throw std::runtime_error(
        "HTTP/3 connections did not drain before shutdown");
  {
    std::error_code ignored;
    (void)std::filesystem::remove(stop_path, ignored);
  }
  std::wcout
      << L"NTL browser HTTP/3 diagnostic stopped: "
      << L"delivered-requests="
      << service.delivered_requests()
      << L", html-files=" << logger.html_files()
      << L", dynamic-hosts=" << service.dynamic_hosts()
      << L", downstream=h3, upstream=h3"
      << L", persistent-browser-changes=none\n";
  return 0;
}

} // namespace crtsys::wfp_sample::browser_https
