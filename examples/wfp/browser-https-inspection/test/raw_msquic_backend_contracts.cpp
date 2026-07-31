#include <msquic.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

#include <ntl/net/http3/msquic_backend>

namespace {

class contract_sink final
    : public ntl::net::http3::quic_backend_sink {
public:
  ntl::status
  on_connected(std::string_view) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_request_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_qpack_encoder_stream(
      ntl::net::scatter_view) noexcept override {
    return ntl::status::ok();
  }
  ntl::status on_peer_bidirectional_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    ++bidirectional;
    return ntl::status::ok();
  }
  ntl::status on_peer_unidirectional_stream(
      std::uint64_t, ntl::net::scatter_view,
      bool) noexcept override {
    ++unidirectional;
    return ntl::status::ok();
  }
  ntl::status on_datagram(
      ntl::net::scatter_view) noexcept override {
    ++datagrams;
    return ntl::status::ok();
  }
  void on_closed(NTSTATUS) noexcept override {}

  std::size_t bidirectional = 0;
  std::size_t unidirectional = 0;
  std::size_t datagrams = 0;
};

static_assert(!std::is_copy_constructible_v<
              ntl::net::http3::msquic_backend::connection>);
static_assert(
    ntl::net::http3::msquic_backend::capabilities.available);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .bidirectional_streams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .unidirectional_streams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities
        .quic_datagrams);
static_assert(
    ntl::net::http3::msquic_backend::capabilities.webtransport ==
    ntl::net::http3::msquic_backend::capabilities.reliable_reset_at);
static_assert(
    !ntl::net::http3::msquic_backend::capabilities
         .arbitrary_browser_server_identity);

[[maybe_unused]] void compile_factories(
    const QUIC_API_TABLE *api, HQUIC registration,
    HQUIC configuration, HQUIC accepted,
    contract_sink &sink) {
  auto client =
      ntl::net::http3::msquic_backend::connection::try_connect(
          api, registration, configuration,
          "example.test", 443, sink);
  auto server =
      ntl::net::http3::msquic_backend::connection::try_accept(
          api, accepted, configuration, sink);
  (void)client;
  (void)server;
}

} // namespace

int main() {
  const auto capabilities =
      ntl::net::http3::msquic_backend::capabilities;
  return capabilities.extended_connect &&
                 capabilities.quic_datagrams &&
                 capabilities.webtransport ==
                     capabilities.reliable_reset_at
             ? 0
             : 1;
}
