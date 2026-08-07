#include <ntl/net/tls/acceptor>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/client_hello>
#include <ntl/net/http/http1_framing>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/net/tls/inspection_frontend>
#include <ntl/net/tls/product_backend>

#include <algorithm>
#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(
      ::WSAGetLastError(), std::system_category(), operation);
}

[[noreturn]] void throw_windows(const char *operation) {
  throw std::system_error(
      static_cast<int>(::GetLastError()),
      std::system_category(), operation);
}

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int status = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (status != 0)
      throw std::system_error(
          status, std::system_category(), "WSAStartup");
  }
  winsock_session(const winsock_session &) = delete;
  winsock_session &operator=(const winsock_session &) = delete;
  ~winsock_session() { (void)::WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() { reset(); }

  SOCKET get() const noexcept { return value_; }
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }

private:
  void reset() noexcept {
    if (value_ != INVALID_SOCKET)
      (void)::closesocket(value_);
    value_ = INVALID_SOCKET;
  }
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port = 0;
};

listener make_listener() {
  socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(listener)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(socket.get(),
             reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR ||
      ::listen(socket.get(), 2) == SOCKET_ERROR)
    throw_socket("bind/listen");

  int size = sizeof(address);
  if (::getsockname(socket.get(),
                    reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
    throw_socket("getsockname");
  return {std::move(socket), ntohs(address.sin_port)};
}

socket_owner connect_loopback(std::uint16_t port) {
  socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(socket.get(),
                reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    throw_socket("connect(loopback)");
  return socket;
}

socket_owner accept_one(const listener &value) {
  socket_owner accepted(
      ::accept(value.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw_socket("accept");
  return accepted;
}

class ephemeral_certificate {
public:
  ephemeral_certificate() {
    container_name_ =
        L"crtsys-ntl-tls-test-" +
        std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64());
    if (!::CryptAcquireContextW(
            &provider_, container_name_.c_str(),
            MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
            CRYPT_NEWKEYSET | CRYPT_SILENT))
      throw_windows("CryptAcquireContextW(test keyset)");
    if (!::CryptGenKey(
            provider_, AT_KEYEXCHANGE,
            (2048u << 16) | CRYPT_EXPORTABLE, &key_))
      throw_windows("CryptGenKey");

    constexpr wchar_t subject[] = L"CN=crtsys NTL TLS test CA";
    DWORD encoded_size = 0;
    if (!::CertStrToNameW(
            X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
            nullptr, nullptr, &encoded_size, nullptr))
      throw_windows("CertStrToNameW(size)");
    std::vector<BYTE> encoded(encoded_size);
    if (!::CertStrToNameW(
            X509_ASN_ENCODING, subject, CERT_X500_NAME_STR,
            nullptr, encoded.data(), &encoded_size, nullptr))
      throw_windows("CertStrToNameW");

    CERT_NAME_BLOB name{
        encoded_size, encoded.data()};
    CRYPT_ALGORITHM_IDENTIFIER signature{};
    signature.pszObjId =
        const_cast<char *>(szOID_RSA_SHA256RSA);
    SYSTEMTIME start{};
    ::GetSystemTime(&start);
    SYSTEMTIME end = start;
    ++end.wYear;
    CRYPT_KEY_PROV_INFO key_info{};
    key_info.pwszContainerName = container_name_.data();
    key_info.pwszProvName =
        const_cast<wchar_t *>(MS_ENH_RSA_AES_PROV_W);
    key_info.dwProvType = PROV_RSA_AES;
    key_info.dwKeySpec = AT_KEYEXCHANGE;

    CERT_BASIC_CONSTRAINTS2_INFO constraints{};
    constraints.fCA = TRUE;
    auto encoded_constraints =
        ntl::net::detail::encode_tls_object(
            X509_BASIC_CONSTRAINTS2, &constraints);
    BYTE usage_bits = static_cast<BYTE>(
        CERT_KEY_CERT_SIGN_KEY_USAGE |
        CERT_CRL_SIGN_KEY_USAGE);
    CRYPT_BIT_BLOB usage{
        sizeof(usage_bits), &usage_bits, 0};
    auto encoded_usage =
        ntl::net::detail::encode_tls_object(
            X509_KEY_USAGE, &usage);
    std::array<CERT_EXTENSION, 2> extensions{};
    extensions[0] = {
        const_cast<char *>(szOID_BASIC_CONSTRAINTS2), TRUE,
        {static_cast<DWORD>(encoded_constraints.size()),
         encoded_constraints.data()}};
    extensions[1] = {
        const_cast<char *>(szOID_KEY_USAGE), TRUE,
        {static_cast<DWORD>(encoded_usage.size()),
         encoded_usage.data()}};
    CERT_EXTENSIONS certificate_extensions{
        static_cast<DWORD>(extensions.size()),
        extensions.data()};

    certificate_ = ::CertCreateSelfSignCertificate(
        static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
        &name, 0, &key_info, &signature, &start, &end,
        &certificate_extensions);
    if (!certificate_)
      throw_windows("CertCreateSelfSignCertificate");
  }

  ephemeral_certificate(const ephemeral_certificate &) = delete;
  ephemeral_certificate &
  operator=(const ephemeral_certificate &) = delete;

  ~ephemeral_certificate() {
    if (certificate_)
      (void)::CertFreeCertificateContext(certificate_);
    if (key_)
      (void)::CryptDestroyKey(key_);
    if (provider_)
      (void)::CryptReleaseContext(provider_, 0);
    if (!container_name_.empty()) {
      HCRYPTPROV deleted = 0;
      (void)::CryptAcquireContextW(
          &deleted, container_name_.c_str(),
          MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
          CRYPT_DELETEKEYSET | CRYPT_SILENT);
    }
  }

  PCCERT_CONTEXT get() const noexcept { return certificate_; }

private:
  HCRYPTPROV provider_ = 0;
  HCRYPTKEY key_ = 0;
  PCCERT_CONTEXT certificate_ = nullptr;
  std::wstring container_name_;
};

template <class T> class coroutine_task {
public:
  struct promise_type;

  struct shared_state {
    shared_state()
        : completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      if (!completed)
        throw_windows("CreateEventW");
    }
    ~shared_state() {
      if (completed)
        (void)::CloseHandle(completed);
    }
    HANDLE completed = nullptr;
    std::optional<T> result;
    std::exception_ptr exception;
  };

  coroutine_task(const coroutine_task &) = delete;
  coroutine_task &operator=(const coroutine_task &) = delete;
  coroutine_task(coroutine_task &&) noexcept = default;
  coroutine_task &operator=(coroutine_task &&) noexcept = default;

  T get() {
    if (!state_)
      throw std::logic_error("coroutine task has no state");
    auto state = std::exchange(state_, {});
    if (::WaitForSingleObject(state->completed, INFINITE) !=
        WAIT_OBJECT_0)
      throw_windows("WaitForSingleObject");
    if (state->exception)
      std::rethrow_exception(state->exception);
    if (!state->result)
      throw std::logic_error("coroutine produced no result");
    return std::move(*state->result);
  }

  struct promise_type {
    coroutine_task get_return_object() noexcept {
      return coroutine_task(state);
    }
    std::suspend_never initial_suspend() const noexcept { return {}; }
    std::suspend_never final_suspend() const noexcept { return {}; }
    template <class U> void return_value(U &&value) {
      state->result.emplace(std::forward<U>(value));
      (void)::SetEvent(state->completed);
    }
    void unhandled_exception() noexcept {
      state->exception = std::current_exception();
      (void)::SetEvent(state->completed);
    }
    std::shared_ptr<shared_state> state =
        std::make_shared<shared_state>();
  };

private:
  explicit coroutine_task(
      std::shared_ptr<shared_state> state) noexcept
      : state_(std::move(state)) {}
  std::shared_ptr<shared_state> state_;
};

std::vector<std::byte> make_pattern(
    std::size_t size, std::uint8_t seed) {
  std::vector<std::byte> result(size);
  for (std::size_t index = 0; index != size; ++index)
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(
            seed + static_cast<std::uint8_t>(index * 29)));
  return result;
}

coroutine_task<std::size_t>
run_server(ntl::net::tls_stream &stream,
           std::span<const std::byte> expected,
           std::span<const std::byte> reply) {
  co_await stream.handshake_server();

  std::array<std::byte, 37> buffer{};
  std::size_t received = 0;
  while (received != expected.size()) {
    const std::size_t count = co_await stream.read_some_borrowed(buffer);
    if (count == 0 || count > expected.size() - received)
      throw std::runtime_error(
          "TLS server received an invalid request size");
    if (std::memcmp(
            buffer.data(), expected.data() + received, count) != 0)
      throw std::runtime_error(
          "TLS server plaintext request mismatch");
    received += count;
  }

  const std::size_t written = co_await stream.write_all(reply);
  if (written != reply.size())
    throw std::runtime_error("TLS server reply completed short");
  co_await stream.shutdown();
  co_return received + written;
}

coroutine_task<std::size_t>
run_probed_server(
    ntl::net::async_socket socket,
    std::shared_ptr<ntl::net::tls_server_identity_provider> provider,
    std::wstring_view expected_server_name,
    std::span<const std::byte> expected,
    std::span<const std::byte> reply) {
  auto accepted = co_await ntl::net::accept_tls(
      std::move(socket), std::move(provider),
      {.maximum_buffered_ciphertext = 128 * 1024,
       .maximum_client_hello = 64 * 1024,
       .receive_chunk_size = 7,
       .maximum_alpn_protocols = 16},
      {.maximum_buffered_ciphertext = 1024 * 1024,
       .receive_chunk_size = 128},
      {.application_protocols = {"h2"},
       .require_application_protocol = false});
  auto &stream = accepted.borrowed_stream();
  if (accepted.client_hello_ref().server_name() !=
          expected_server_name ||
      !accepted.borrowed_certificate() ||
      (!stream.negotiated_application_protocol().empty() &&
       stream.negotiated_application_protocol() != "h2"))
    throw std::runtime_error(
        "SNI TLS acceptor did not select an identity");

  ntl::net::tls_framed_stream framed(
      stream, ntl::net::framing::fixed_size_framer(expected.size()),
      ntl::net::framing::frame_limits{expected.size()}, 37);
  const auto request = co_await framed.read_frame();
  if (request.frame().size() != expected.size() ||
      std::memcmp(
          request.frame().data(), expected.data(), expected.size()) != 0)
    throw std::runtime_error(
        "probed TLS framed request mismatch");

  const std::size_t written = co_await stream.write_all(reply);
  if (written != reply.size())
    throw std::runtime_error(
        "probed TLS server reply completed short");
  co_await stream.shutdown();
  co_return request.size() + written;
}

coroutine_task<std::size_t>
run_client(ntl::net::tls_stream &stream,
           std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy,
           std::wstring server_name,
           std::span<const std::byte> request,
           std::span<const std::byte> expected_reply) {
  co_await stream.handshake_client({
      .server_name = std::move(server_name),
      .certificate_policy = std::move(policy),
      .application_protocols = {"h2", "http/1.1"},
      .require_application_protocol = false});
  if (!stream.negotiated_application_protocol().empty() &&
      stream.negotiated_application_protocol() != "h2")
    throw std::runtime_error(
        "Schannel did not negotiate the expected ALPN protocol");
  const auto connection = stream.connection_info();
  if (connection.protocol == 0 ||
      connection.cipher_algorithm == 0 ||
      connection.cipher_strength == 0)
    throw std::runtime_error(
        "Schannel connection attributes were empty");

  const std::size_t written =
      co_await stream.write_all(request);
  if (written != request.size())
    throw std::runtime_error("TLS client request completed short");

  std::array<std::byte, 113> buffer{};
  std::size_t received = 0;
  while (received != expected_reply.size()) {
    const std::size_t count = co_await stream.read_some_borrowed(buffer);
    if (count == 0 ||
        count > expected_reply.size() - received)
      throw std::runtime_error(
          "TLS client received an invalid reply size");
    if (std::memcmp(
            buffer.data(),
            expected_reply.data() + received, count) != 0)
      throw std::runtime_error(
          "TLS client plaintext reply mismatch");
    received += count;
  }

  if (co_await stream.read_some_borrowed(buffer) != 0 ||
      !stream.received_close_notify())
    throw std::runtime_error(
        "TLS client did not receive close_notify");
  co_await stream.shutdown();
  co_return written + received;
}

class rejecting_policy final
    : public ntl::net::tls_peer_certificate_policy {
public:
  bool verify(PCCERT_CONTEXT,
              std::wstring_view) noexcept override {
    return false;
  }
};

class alpn_observing_identity_provider final
    : public ntl::net::tls_server_identity_provider {
public:
  explicit alpn_observing_identity_provider(
      std::shared_ptr<ntl::net::cached_tls_server_identity_provider> inner)
      : inner_(std::move(inner)) {}

  std::shared_ptr<ntl::net::tls_server_identity>
  select(const ntl::net::tls_client_hello &hello) override {
    const auto &protocols = hello.application_protocols();
    if (protocols.size() != 2 || protocols[0] != "h2" ||
        protocols[1] != "http/1.1")
      throw std::runtime_error(
          "Schannel ClientHello did not contain the configured ALPN offer");
    return inner_->select(hello);
  }

private:
  std::shared_ptr<ntl::net::cached_tls_server_identity_provider> inner_;
};

coroutine_task<bool>
run_rejected_client(ntl::net::tls_stream &stream,
                    std::shared_ptr<rejecting_policy> policy) {
  try {
    co_await stream.handshake_client(
        {L"localhost", std::move(policy)});
  } catch (const std::system_error &) {
    co_return true;
  }
  co_return false;
}

coroutine_task<bool>
run_handshake_only_server(ntl::net::tls_stream &stream) {
  try {
    co_await stream.handshake_server();
    co_return stream.is_handshaken();
  } catch (const std::system_error &) {
    co_return false;
  }
}

coroutine_task<bool> run_mtls_server(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_client_certificate_policy> policy) {
  co_await stream.handshake_server(
      {.require_client_certificate = true,
       .client_certificate_policy = std::move(policy)});
  std::array<std::byte, 1> request{};
  if (co_await stream.read_some_borrowed(request) != 1 ||
      request[0] != std::byte{0x5a})
    throw std::runtime_error(
        "mTLS server did not receive the authenticated request");
  constexpr std::array reply{std::byte{0xa5}};
  if (co_await stream.write_all(reply) != reply.size())
    throw std::runtime_error("mTLS server reply completed short");
  co_await stream.shutdown();
  co_return true;
}

coroutine_task<bool> run_mtls_client(
    ntl::net::tls_stream &stream,
    std::shared_ptr<ntl::net::tls_peer_certificate_policy> policy) {
  co_await stream.handshake_client(
      {.server_name = L"localhost",
       .certificate_policy = std::move(policy)});
  constexpr std::array request{std::byte{0x5a}};
  if (co_await stream.write_all(request) != request.size())
    throw std::runtime_error("mTLS client request completed short");
  std::array<std::byte, 1> reply{};
  if (co_await stream.read_some_borrowed(reply) != 1 ||
      reply[0] != std::byte{0xa5})
    throw std::runtime_error(
        "mTLS client did not receive the authenticated reply");
  if (co_await stream.read_some_borrowed(reply) != 0)
    throw std::runtime_error("mTLS server did not close cleanly");
  co_await stream.shutdown();
  co_return true;
}

static_assert(std::is_copy_constructible_v<
              ntl::net::tls_credentials>);
static_assert(std::is_move_constructible_v<
              ntl::net::tls_credentials>);
static_assert(!std::is_copy_constructible_v<
              ntl::net::tls_stream>);
static_assert(std::is_move_constructible_v<
              ntl::net::tls_stream>);

void test_http1_framing() {
  const auto probe = [](ntl::net::http::http1_message_framer &framer,
                        std::string_view text) {
    const auto bytes = std::as_bytes(std::span(text));
    return framer.probe(
        ntl::net::scatter_view::from_contiguous(bytes));
  };

  ntl::net::http::http1_message_framer request(
      ntl::net::http::http1_message_kind::request,
      {.maximum_header_size = 1024,
       .maximum_body_size = 1024,
       .maximum_chunk_line_size = 128,
       .maximum_trailer_size = 256});
  constexpr std::string_view content_length =
      "POST /inspect HTTP/1.1\r\nHost: auto.example.test\r\n"
      "Content-Length: 7\r\n\r\nBLOCKME";
  if (probe(request, content_length.substr(
                         0, content_length.size() - 1))
          .state() != ntl::net::framing::probe_state::need_more ||
      probe(request, content_length).state() !=
          ntl::net::framing::probe_state::complete)
    throw std::runtime_error(
        "HTTP/1 Content-Length framing contract failed");

  constexpr std::string_view chunked =
      "POST /inspect HTTP/1.1\r\nHost: auto.example.test\r\n"
      "Transfer-Encoding: gzip, chunked\r\n\r\n"
      "4\r\nWiki\r\n5\r\npedia\r\n0\r\nProof: yes\r\n\r\n";
  const auto chunked_probe = probe(request, chunked);
  if (chunked_probe.state() !=
          ntl::net::framing::probe_state::complete ||
      chunked_probe.frame_size() != chunked.size())
    throw std::runtime_error(
        "HTTP/1 chunked framing contract failed");

  constexpr std::string_view conflicting =
      "POST / HTTP/1.1\r\nContent-Length: 1\r\n"
      "Content-Length: 2\r\n\r\nxx";
  if (probe(request, conflicting).state() !=
      ntl::net::framing::probe_state::malformed)
    throw std::runtime_error(
        "conflicting HTTP Content-Length was accepted");

  ntl::net::http::http1_message_framer response(
      ntl::net::http::http1_message_kind::response);
  constexpr std::string_view close_delimited =
      "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody";
  if (probe(response, close_delimited).state() !=
      ntl::net::framing::probe_state::malformed)
    throw std::runtime_error(
        "ambiguous close-delimited HTTP response was accepted");

  ntl::net::http::http1_message_framer head_response(
      ntl::net::http::http1_message_kind::response,
      {.maximum_header_size = 1024,
       .maximum_body_size = 1024,
       .maximum_chunk_line_size = 128,
       .maximum_trailer_size = 128,
       .response_body_forbidden = true});
  constexpr std::string_view head_metadata =
      "HTTP/1.1 200 OK\r\nContent-Length: 554\r\n"
      "Content-Encoding: gzip\r\n\r\n";
  const auto head_probe = probe(head_response, head_metadata);
  if (head_probe.state() !=
          ntl::net::framing::probe_state::complete ||
      head_probe.frame_size() != head_metadata.size())
    throw std::runtime_error(
        "HEAD response representation metadata was treated as a body");

  constexpr std::string_view not_modified =
      "HTTP/1.1 304 Not Modified\r\n"
      "Content-Length: 554\r\n\r\n";
  const auto not_modified_probe = probe(response, not_modified);
  if (not_modified_probe.state() !=
          ntl::net::framing::probe_state::complete ||
      not_modified_probe.frame_size() != not_modified.size())
    throw std::runtime_error(
        "304 response representation metadata was treated as a body");

  constexpr std::string_view content_length_with_ows =
      "HTTP/1.1 200 OK\r\nContent-Length:554 \r\n"
      "Server: ePrism\r\nConnection: close\r\n\r\n"
      "body";
  const auto ows_probe =
      probe(response, content_length_with_ows);
  if (ows_probe.state() !=
          ntl::net::framing::probe_state::need_more ||
      ows_probe.required_total() !=
          content_length_with_ows.size() - 4 + 554)
    throw std::runtime_error(
        "HTTP/1 Content-Length OWS framing contract failed");

  ntl::net::http::http1_message_framer close_response(
      ntl::net::http::http1_message_kind::response,
      {.maximum_header_size = 1024,
       .maximum_body_size = 1024,
       .maximum_chunk_line_size = 128,
       .maximum_trailer_size = 128,
       .allow_close_delimited_response = true});
  const auto close_bytes = ntl::net::scatter_view::from_contiguous(
      std::as_bytes(std::span(close_delimited)));
  if (ntl::net::framing::probe(
          close_response, close_bytes,
          {.maximum_frame_size = 2048})
          .state() != ntl::net::framing::probe_state::need_more)
    throw std::runtime_error(
        "close-delimited HTTP response completed before EOF");
  const auto close_final = ntl::net::framing::finish(
      close_response, close_bytes,
      {.maximum_frame_size = 2048});
  if (close_final.state() !=
          ntl::net::framing::probe_state::complete ||
      close_final.frame_size() != close_delimited.size())
    throw std::runtime_error(
        "close-delimited HTTP response did not finalize at EOF");
}

class test_ech_channel final
    : public ntl::net::inspection::ech_plaintext_channel {
public:
  ntl::result<std::size_t>
  read(std::span<std::byte>) noexcept override {
    return ntl::ok(std::size_t{0});
  }
  ntl::result<std::size_t>
  write(std::span<const std::byte> source) noexcept override {
    return ntl::ok(source.size());
  }
  ntl::status shutdown() noexcept override {
    return ntl::status::ok();
  }
};

bool test_tls_frontend_boundaries() {
  ntl::net::inspection::tls_inspection_observation_view observation;
  observation.server_name = L"outer.example";
  observation.protocol_adapter_available = true;
  const ntl::net::inspection::ech_frontend_result confirmed{
      ntl::net::inspection::ech_offer_state::confirmed, {}, {}};
  if (!ntl::net::inspection::apply_ech_result(
           observation, confirmed)
           .is_ok())
    return false;
  const ntl::net::inspection::explicit_tls_inspection_policy policy;
  const auto blocked = policy.decide(observation);
  if (blocked.issue !=
          ntl::net::inspection::tls_inspection_issue::
              encrypted_client_hello ||
      blocked.action !=
          ntl::net::inspection::tls_inspection_action::block)
    return false;

  const ntl::net::inspection::ech_frontend_result decrypted{
      ntl::net::inspection::ech_offer_state::decrypted,
      L"inner.example", {"h2"},
      std::make_shared<test_ech_channel>()};
  if (!ntl::net::inspection::apply_ech_result(
           observation, decrypted)
           .is_ok() ||
      observation.server_name != L"inner.example")
    return false;
  const auto inspect = policy.decide(observation);
  return inspect.action ==
             ntl::net::inspection::tls_inspection_action::inspect &&
         inspect.issue ==
             ntl::net::inspection::tls_inspection_issue::none;
}

bool test_product_tls_backend_audit() {
  using namespace ntl::net::inspection;
  auto audit = std::make_shared<bounded_tls_audit_sink>(2);
  auto unavailable =
      std::make_shared<unavailable_origin_client_identity>();
  auto provider = std::make_shared<
      audited_origin_client_identity_provider>(unavailable, audit);
  const auto selected = provider->select({L"mtls.example", {}});
  if (!selected || static_cast<bool>(*selected))
    return false;
  provider->close();
  provider->close();
  const auto rejected =
      provider->select({L"after-close.example", {}});
  if (rejected || rejected.status() != STATUS_DELETE_PENDING)
    return false;

  auto race_audit = std::make_shared<bounded_tls_audit_sink>(2);
  auto race_provider = std::make_shared<
      audited_origin_client_identity_provider>(unavailable, race_audit);
  std::atomic<bool> start{false};
  std::atomic<unsigned> selections_before_close{0};
  std::atomic<unsigned> rejected_after_close{0};
  std::atomic<bool> unexpected_status{false};
  std::thread selector(
      [retained = race_provider, &start, &selections_before_close,
       &rejected_after_close, &unexpected_status] {
        while (!start.load(std::memory_order_acquire))
          std::this_thread::yield();
        for (;;) {
          const auto result = retained->select({L"mtls-race.example", {}});
          if (result) {
            if (static_cast<bool>(*result)) {
              unexpected_status.store(true, std::memory_order_release);
              return;
            }
            selections_before_close.fetch_add(1, std::memory_order_release);
            continue;
          }
          if (result.status() == STATUS_DELETE_PENDING) {
            rejected_after_close.fetch_add(1, std::memory_order_release);
            return;
          }
          unexpected_status.store(true, std::memory_order_release);
          return;
        }
      });
  start.store(true, std::memory_order_release);
  while (selections_before_close.load(std::memory_order_acquire) == 0)
    std::this_thread::yield();
  race_provider->close();
  race_provider->close();
  race_provider.reset();
  selector.join();
  if (unexpected_status.load(std::memory_order_acquire) ||
      rejected_after_close.load(std::memory_order_acquire) != 1)
    return false;

  audit->record({tls_audit_kind::blocked_confirmed_ech,
                 std::chrono::system_clock::now(), L"ech.example",
                 STATUS_ACCESS_DENIED});
  audit->record({tls_audit_kind::downstream_identity_selected,
                 std::chrono::system_clock::now(), L"site.example",
                 STATUS_SUCCESS});
  const auto snapshot = audit->snapshot();
  return snapshot.size() == 2 && audit->discarded() == 1 &&
         snapshot.front().kind == tls_audit_kind::blocked_confirmed_ech &&
         snapshot.back().kind ==
             tls_audit_kind::downstream_identity_selected;
}

std::array<BYTE, 32>
certificate_spki_sha256(PCCERT_CONTEXT certificate) {
  if (!certificate || !certificate->pCertInfo)
    throw std::invalid_argument(
        "SPKI test requires a certificate");
  std::array<BYTE, 32> result{};
  DWORD size = static_cast<DWORD>(result.size());
  if (!::CryptHashPublicKeyInfo(
          0, CALG_SHA_256, 0, X509_ASN_ENCODING,
          &certificate->pCertInfo->SubjectPublicKeyInfo,
          result.data(), &size) ||
      size != result.size())
    throw_windows("CryptHashPublicKeyInfo(shared leaf)");
  return result;
}

} // namespace

int main() {
  try {
    if (!test_tls_frontend_boundaries())
      throw std::runtime_error(
          "TLS frontend policy boundaries failed");
    if (!test_product_tls_backend_audit())
      throw std::runtime_error("product TLS backend audit failed");
    test_http1_framing();
    winsock_session winsock;
    bool rejected_unscoped_revocation_exception = false;
    try {
      auto invalid_revocation_credentials =
          ntl::net::tls_credentials::client(
              {.ignore_missing_revocation_information = true});
      (void)invalid_revocation_credentials;
    } catch (const std::invalid_argument &) {
      rejected_unscoped_revocation_exception = true;
    }
    if (!rejected_unscoped_revocation_exception)
      throw std::runtime_error(
          "unscoped TLS revocation exception was accepted");
    bool rejected_manual_revocation_overlap = false;
    try {
      auto invalid_manual_credentials =
          ntl::net::tls_credentials::client(
              {.manual_peer_validation = true,
               .revocation_check =
                   ntl::net::tls_certificate_revocation_check::
                       chain});
      (void)invalid_manual_credentials;
    } catch (const std::invalid_argument &) {
      rejected_manual_revocation_overlap = true;
    }
    if (!rejected_manual_revocation_overlap)
      throw std::runtime_error(
          "manual TLS validation accepted an automatic revocation policy");
    auto availability_tolerant_credentials =
        ntl::net::tls_credentials::client(
            {.revocation_check =
                 ntl::net::tls_certificate_revocation_check::
                     chain_excluding_root,
             .ignore_missing_revocation_information = true,
             .ignore_offline_revocation = true});
    (void)availability_tolerant_credentials;
    ephemeral_certificate certificate;
    ntl::net::windows_tls_certificate_issuer
        shared_key_issuer(
            certificate.get(),
            {.key_name_prefix =
                 L"crtsys-ntl-shared-leaf-contract",
             .rsa_bits = 2048,
             .validity_days = 2,
             .machine_keys = false,
             .reuse_leaf_key = true});
    auto shared_first =
        shared_key_issuer.issue(L"first.example.test");
    auto shared_second =
        shared_key_issuer.issue(L"second.example.test");
    if (certificate_spki_sha256(shared_first.borrowed_certificate()) !=
        certificate_spki_sha256(shared_second.borrowed_certificate()))
      throw std::runtime_error(
          "shared leaf issuer changed SPKI between hosts");

    auto issuer = std::make_shared<
        ntl::net::windows_tls_certificate_issuer>(
        certificate.get(),
        ntl::net::windows_tls_certificate_issuer_options{
            .key_name_prefix = L"crtsys-ntl-tls-contract",
            .rsa_bits = 2048,
            .validity_days = 2,
            .machine_keys = false});
    auto identities = std::make_shared<
        ntl::net::cached_tls_server_identity_provider>(issuer, 2);
    auto observed_identities =
        std::make_shared<alpn_observing_identity_provider>(identities);

    {
      auto frontend = std::make_shared<
          ntl::net::inspection::managed_tls_frontend>(
          identities,
          std::make_shared<
              ntl::net::inspection::unavailable_ech_frontend>(),
          std::make_shared<
              ntl::net::inspection::inspectable_downstream_trust>(),
          std::make_shared<
              ntl::net::inspection::null_tls_audit_sink>());
      std::atomic<unsigned> selections_before_close{0};
      std::atomic<unsigned> rejected_after_close{0};
      std::atomic<bool> unexpected_frontend_status{false};
      const ntl::net::tls_client_hello empty_hello;
      std::thread selector(
          [retained = frontend, &selections_before_close,
           &rejected_after_close, &unexpected_frontend_status,
           &empty_hello] {
            for (;;) {
              const auto selected = retained->select(empty_hello, {});
              if (selected) {
                unexpected_frontend_status.store(true,
                                                 std::memory_order_release);
                return;
              }
              const auto status = static_cast<NTSTATUS>(selected.status());
              if (status == STATUS_DELETE_PENDING) {
                rejected_after_close.fetch_add(1, std::memory_order_release);
                return;
              }
              if (status != STATUS_NOT_FOUND) {
                unexpected_frontend_status.store(true,
                                                 std::memory_order_release);
                return;
              }
              selections_before_close.fetch_add(1,
                                                std::memory_order_release);
            }
          });
      while (selections_before_close.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
      frontend->close();
      frontend->close();
      frontend.reset();
      selector.join();
      if (unexpected_frontend_status.load(std::memory_order_acquire) ||
          rejected_after_close.load(std::memory_order_acquire) != 1)
        throw std::runtime_error(
            "managed TLS frontend close/use lifetime contract failed");
      auto closed_frontend = std::make_shared<
          ntl::net::inspection::managed_tls_frontend>(
          identities,
          std::make_shared<
              ntl::net::inspection::unavailable_ech_frontend>(),
          std::make_shared<
              ntl::net::inspection::inspectable_downstream_trust>(),
          std::make_shared<
              ntl::net::inspection::null_tls_audit_sink>());
      closed_frontend->close();
      const auto rejected = closed_frontend->select(empty_hello, {});
      if (rejected || rejected.status() != STATUS_DELETE_PENDING)
        throw std::runtime_error(
            "closed managed TLS frontend accepted new work");
    }

    {
      auto closing_cache = std::make_shared<
          ntl::net::cached_tls_server_identity_provider>(issuer, 2);
      auto retained_identity =
          closing_cache->select(L"cache-race.example.test");
      auto retained_credentials = retained_identity->credentials();
      std::atomic<bool> start{false};
      std::atomic<unsigned> successful_selections{0};
      std::atomic<unsigned> rejected_selections{0};
      std::atomic<bool> unexpected_error{false};
      std::thread selector([&] {
        while (!start.load(std::memory_order_acquire))
          std::this_thread::yield();
        for (;;) {
          try {
            auto selected =
                closing_cache->select(L"cache-race.example.test");
            if (!selected || !selected->credentials()) {
              unexpected_error.store(true, std::memory_order_release);
              return;
            }
            successful_selections.fetch_add(1, std::memory_order_release);
          } catch (const std::system_error &error) {
            if (error.code().value() == ERROR_OPERATION_ABORTED) {
              rejected_selections.fetch_add(1, std::memory_order_release);
              return;
            }
            unexpected_error.store(true, std::memory_order_release);
            return;
          } catch (...) {
            unexpected_error.store(true, std::memory_order_release);
            return;
          }
        }
      });
      start.store(true, std::memory_order_release);
      while (successful_selections.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
      closing_cache->close();
      closing_cache->close();
      selector.join();

      if (unexpected_error.load(std::memory_order_acquire) ||
          rejected_selections.load(std::memory_order_acquire) != 1 ||
          !retained_identity->borrowed_certificate() ||
          !retained_credentials)
        throw std::runtime_error(
            "TLS identity cache close/use lifetime contract failed");
      try {
        (void)closing_cache->select(L"after-close.example.test");
        throw std::runtime_error(
            "closed TLS identity cache accepted new work");
      } catch (const std::system_error &error) {
        if (error.code().value() != ERROR_OPERATION_ABORTED)
          throw;
      }
    }

    {
      auto bounded_cache = std::make_shared<
          ntl::net::cached_tls_server_identity_provider>(issuer, 2);
      auto first = bounded_cache->select(L"lru-first.example.test");
      auto second = bounded_cache->select(L"lru-second.example.test");
      auto third = bounded_cache->select(L"lru-third.example.test");
      if (!first || !second || !third || bounded_cache->size() != 2)
        throw std::runtime_error("TLS identity cache capacity failed");

      auto reissued_first =
          bounded_cache->select(L"lru-first.example.test");
      if (!reissued_first || reissued_first == first ||
          bounded_cache->size() != 2 ||
          !first->borrowed_certificate() || !first->credentials())
        throw std::runtime_error(
            "TLS identity cache eviction ownership failed");

      std::shared_ptr<ntl::net::tls_server_identity> concurrent_first;
      std::shared_ptr<ntl::net::tls_server_identity> concurrent_second;
      std::atomic<bool> select_start{false};
      std::thread first_selector([&] {
        while (!select_start.load(std::memory_order_acquire))
          std::this_thread::yield();
        concurrent_first =
            bounded_cache->select(L"concurrent.example.test");
      });
      std::thread second_selector([&] {
        while (!select_start.load(std::memory_order_acquire))
          std::this_thread::yield();
        concurrent_second =
            bounded_cache->select(L"concurrent.example.test");
      });
      select_start.store(true, std::memory_order_release);
      first_selector.join();
      second_selector.join();
      if (!concurrent_first || concurrent_first != concurrent_second ||
          bounded_cache->size() != 2)
        throw std::runtime_error(
            "TLS identity cache concurrent selection failed");

      auto retained_identity = concurrent_first;
      auto retained_credentials = retained_identity->credentials();
      bounded_cache->close();
      bounded_cache.reset();
      if (!retained_identity->borrowed_certificate() ||
          !retained_credentials)
        throw std::runtime_error(
            "TLS identity did not outlive its cache facade");
    }
    auto client_credentials =
        ntl::net::tls_credentials::client(
            {.manual_peer_validation = true});
    ntl::net::windows_tls_certificate_issuer client_issuer(
        certificate.get(),
        {.key_name_prefix = L"crtsys-ntl-mtls-client",
         .rsa_bits = 2048,
         .validity_days = 2,
         .machine_keys = false,
         .certificate_purpose =
             ntl::net::windows_tls_certificate_issuer_options::
                 purpose::client_authentication});
    auto client_certificate =
        client_issuer.issue(L"client.example.test");
    auto mtls_client_credentials =
        ntl::net::tls_credentials::client(
            {.manual_peer_validation = true,
             .borrowed_certificate =
                 client_certificate.borrowed_certificate()});
    auto client_identity =
        std::make_shared<ntl::net::exact_client_certificate_policy>(
            client_certificate.borrowed_certificate());
    ntl::net::inspection::mapped_origin_client_identity
        mapped_origin_identity;
    mapped_origin_identity.add(
        L"mtls.example.test", client_certificate.borrowed_certificate());
    const auto mapped_selection =
        mapped_origin_identity.select(
            {.server_name = L"MTLS.Example.Test.",
             .acceptable_issuers = {}});
    const auto missing_selection =
        mapped_origin_identity.select(
            {.server_name = L"other.example.test",
             .acceptable_issuers = {}});
    if (!mapped_selection || !*mapped_selection ||
        !missing_selection || *missing_selection)
      throw std::runtime_error(
          "mapped origin mTLS identity selection failed");

    const std::array<std::byte, 3> application_id{
        std::byte{1}, std::byte{2}, std::byte{3}};
    ntl::net::inspection::configured_downstream_trust
        downstream_trust;
    downstream_trust.add(
        application_id, L"pinned.example.test",
        ntl::net::inspection::downstream_trust_state::pinned);
    if (downstream_trust.classify(
            {.application_id = application_id,
             .server_name = L"PINNED.Example.Test."}) !=
            ntl::net::inspection::downstream_trust_state::pinned ||
        downstream_trust.classify(
            {.application_id = application_id,
             .server_name = L"unknown.example.test"}) !=
            ntl::net::inspection::downstream_trust_state::unknown)
      throw std::runtime_error(
          "configured downstream trust selection failed");
    auto authority =
        std::make_shared<ntl::net::certificate_authority_policy>(
            certificate.get());

    const auto request = make_pattern(128 * 1024 + 31, 0x31);
    const auto reply = make_pattern(96 * 1024 + 17, 0x92);

    {
      auto listener = make_listener();
      auto client_socket = connect_loopback(listener.port);
      auto server_socket = accept_one(listener);
      ntl::net::io_completion_context context;
      ntl::net::async_socket client(
          context, client_socket.release());
      ntl::net::tls_stream stream(client, client_credentials);
      std::atomic<bool> start{false};
      std::atomic<bool> closed{false};
      std::atomic<unsigned> observations{0};
      std::atomic<bool> rejected{false};
      std::atomic<bool> unexpected{false};
      std::thread user([&] {
        while (!start.load(std::memory_order_acquire))
          std::this_thread::yield();
        while (!closed.load(std::memory_order_acquire)) {
          (void)stream.is_handshaken();
          (void)stream.maximum_plaintext_record();
          stream.cancel();
          observations.fetch_add(1, std::memory_order_release);
        }
        try {
          std::array<std::byte, 1> byte{};
          (void)stream.read_some_borrowed(byte);
          unexpected.store(true, std::memory_order_release);
        } catch (const std::system_error &error) {
          rejected.store(
              error.code().value() == ERROR_OPERATION_ABORTED,
              std::memory_order_release);
        } catch (...) {
          unexpected.store(true, std::memory_order_release);
        }
      });
      start.store(true, std::memory_order_release);
      while (observations.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
      stream.close();
      stream.close();
      closed.store(true, std::memory_order_release);
      user.join();
      context.close();
      if (!rejected.load(std::memory_order_acquire) ||
          unexpected.load(std::memory_order_acquire))
        throw std::runtime_error(
            "TLS same-facade close/use race contract failed");
    }

    {
      auto listener = make_listener();
      auto client_socket = connect_loopback(listener.port);
      auto server_socket = accept_one(listener);
      ntl::net::io_completion_context context;
      ntl::net::async_socket client(
          context, client_socket.release());
      ntl::net::async_socket server(
          context, server_socket.release());
      ntl::net::tls_stream client_tls(
          client, client_credentials);

      auto server_task =
          run_probed_server(
              std::move(server), observed_identities, L"auto.example.test",
              request, reply);
      auto client_task =
          run_client(
              client_tls, authority, L"auto.example.test",
              request, reply);
      const std::size_t client_bytes = client_task.get();
      const std::size_t server_bytes = server_task.get();
      context.wait_for_idle();
      const std::size_t total = request.size() + reply.size();
      if (client_bytes != total || server_bytes != total)
        throw std::runtime_error(
            "TLS byte accounting mismatch");
    }

    {
      auto listener = make_listener();
      auto client_socket = connect_loopback(listener.port);
      auto server_socket = accept_one(listener);
      ntl::net::io_completion_context context;
      ntl::net::async_socket client(
          context, client_socket.release());
      ntl::net::async_socket server(
          context, server_socket.release());
      auto localhost_identity =
          identities->select(L"localhost");
      auto retained_server_credentials =
          localhost_identity->credentials();
      localhost_identity.reset();
      identities->clear();
      ntl::net::tls_stream client_tls(
          client, mtls_client_credentials);
      ntl::net::tls_stream server_tls(
          server, std::move(retained_server_credentials));

      auto server_task =
          run_mtls_server(server_tls, client_identity);
      auto client_task =
          run_mtls_client(client_tls, authority);
      if (!client_task.get() || !server_task.get())
        throw std::runtime_error(
            "mutual TLS identity contract did not complete");
      context.wait_for_idle();
    }

    {
      auto listener = make_listener();
      auto client_socket = connect_loopback(listener.port);
      auto server_socket = accept_one(listener);
      ntl::net::io_completion_context context;
      ntl::net::async_socket client(
          context, client_socket.release());
      ntl::net::async_socket server(
          context, server_socket.release());
      ntl::net::tls_stream client_tls(
          client, client_credentials,
          {.maximum_buffered_ciphertext = 1024 * 1024,
           .receive_chunk_size = 128});
      auto localhost_identity =
          identities->select(L"localhost");
      ntl::net::tls_stream server_tls(
          server, localhost_identity->credentials(),
          {.maximum_buffered_ciphertext = 1024 * 1024,
           .receive_chunk_size = 128});
      auto reject = std::make_shared<rejecting_policy>();

      auto server_task =
          run_handshake_only_server(server_tls);
      auto client_task =
          run_rejected_client(client_tls, reject);
      if (!client_task.get())
        throw std::runtime_error(
            "custom TLS certificate rejection was not enforced");
      client.close();
      (void)server_task.get();
      context.wait_for_idle();
    }

    std::printf(
        "NTL TLS stream ok: request=%zu, reply=%zu, "
        "private-ca=accepted, custom-reject=blocked, "
        "alpn=offered, mtls=exact-client, framed-tls=fragment-safe, "
        "ech=provider-boundary, pinning=explicit-policy, "
        "revocation=explicit-policy, "
        "http1=bounded, shared-leaf-spki=stable, "
        "credential-cache-owner-first=safe, "
        "identity-cache-close-race=safe, frontend-close-race=safe, "
        "stream-close-use-race=safe, "
        "close-notify=both\n",
        request.size(), reply.size());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(
        stderr, "NTL TLS stream failed: %s\n", error.what());
    return 1;
  }
}
