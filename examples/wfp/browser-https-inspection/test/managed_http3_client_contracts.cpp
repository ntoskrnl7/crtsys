#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include <ntl/net/http3/msh3_client>

namespace client = ntl::net::http3::msh3_client;

namespace {

static_assert(
    static_cast<std::uint32_t>(
        STATUS_QUIC_TLS_CERTIFICATE_UNKNOWN) ==
    0xC024012Eu);
static_assert(
    static_cast<std::uint32_t>(
        STATUS_QUIC_TLS_UNKNOWN_CA) ==
    0xC0240130u);

bool test_request_validation() {
  client::client_limits limits;
  client::request request;
  request.server_name = "example.test";
  request.peer = client::peer_endpoint::ipv4_loopback(443);
  request.headers.push_back({"accept", "text/html"});
  if (!client::detail::validate_request(request, limits).is_ok())
    return false;

  request.peer->port = 0;
  if (client::detail::validate_request(
          request, limits) != STATUS_INVALID_PARAMETER)
    return false;
  request.peer->port = 443;

  request.headers[0].name = "Accept";
  if (client::detail::validate_request(
          request, limits) != STATUS_DATA_ERROR)
    return false;
  return true;
}

bool test_synchronous_receive_consumes_once() {
  client::client_limits limits;
  limits.maximum_response_body_bytes = 16;
  client::detail::request_state state(limits);

  const std::byte payload[]{
      std::byte{'N'}, std::byte{'T'}, std::byte{'L'}};
  MSH3_REQUEST_EVENT data{};
  data.Type = MSH3_REQUEST_EVENT_DATA_RECEIVED;
  data.DATA_RECEIVED.Data =
      reinterpret_cast<const std::uint8_t *>(payload);
  data.DATA_RECEIVED.Length =
      static_cast<std::uint32_t>(sizeof(payload));
  if (client::detail::request_callback(
          nullptr, &state, &data) != MSH3_STATUS_SUCCESS ||
      state.message.body.size() != sizeof(payload))
    return false;

  const std::string status_name = ":status";
  const std::string status_value = "200";
  MSH3_HEADER status_header{
      status_name.data(), status_name.size(),
      status_value.data(), status_value.size()};
  MSH3_REQUEST_EVENT header{};
  header.Type = MSH3_REQUEST_EVENT_HEADER_RECEIVED;
  header.HEADER_RECEIVED.Header = &status_header;
  if (client::detail::request_callback(
          nullptr, &state, &header) != MSH3_STATUS_SUCCESS ||
      state.message.status != 200)
    return false;

  MSH3_REQUEST_EVENT complete{};
  complete.Type = MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN;
  if (client::detail::request_callback(
          nullptr, &state, &complete) != MSH3_STATUS_SUCCESS ||
      !state.response_complete ||
      !state.failure.is_ok())
    return false;

  return true;
}

} // namespace

int main() {
  if (!test_request_validation() ||
      !test_synchronous_receive_consumes_once()) {
    std::cerr
        << "NTL msh3 managed client contract failed\n";
    return 1;
  }
  std::cout
      << "NTL msh3 managed client contract passed\n";
  return 0;
}
