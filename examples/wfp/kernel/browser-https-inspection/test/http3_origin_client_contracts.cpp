#include <ntl/net/kernel/http3_origin_client>

#include <type_traits>
#include <utility>

namespace {

using namespace ntl::net::kernel;
using exchange = kernel_http3_origin_exchange;
using request = ntl::net::http::request_message;
using peer_policy =
    ntl::net::kernel::schannel_peer_certificate_policy;

static_assert(noexcept(exchange::try_create(
    std::declval<ntl::net::http3::msquic_backend::connection_context>(),
    std::declval<std::string_view>(),
    std::declval<std::uint16_t>(), std::declval<request &&>(),
    std::declval<std::shared_ptr<peer_policy>>(),
    std::declval<http3_origin_exchange_options>())));
static_assert(!std::is_copy_constructible_v<exchange>);
static_assert(noexcept(std::declval<exchange &>().cancel()));
static_assert(std::is_same_v<
              decltype(std::declval<exchange &>().run_classified()),
              http3_origin_attempt>);
static_assert(!std::is_copy_constructible_v<http3_origin_attempt>);
static_assert(!std::is_default_constructible_v<http3_origin_attempt>);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_CONNECTION_TIMEOUT)) ==
              http3_origin_failure_kind::transport_timeout);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_CONNECTION_IDLE)) ==
              http3_origin_failure_kind::transport_timeout);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_UNREACHABLE)) ==
              http3_origin_failure_kind::transport_connection);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_CONNECTION_REFUSED)) ==
              http3_origin_failure_kind::transport_connection);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_HANDSHAKE_FAILURE)) ==
              http3_origin_failure_kind::peer_authentication);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_REQUIRED_CERTIFICATE)) ==
              http3_origin_failure_kind::peer_authentication);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_BAD_CERTIFICATE)) ==
              http3_origin_failure_kind::peer_authentication);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_ALPN_NEG_FAILURE)) ==
              http3_origin_failure_kind::application_protocol);
static_assert(classify_http3_origin_connection_status(
                  static_cast<NTSTATUS>(QUIC_STATUS_VER_NEG_ERROR)) ==
              http3_origin_failure_kind::application_protocol);
static_assert(classify_http3_origin_connection_status(
                  STATUS_CONNECTION_ABORTED) ==
              http3_origin_failure_kind::internal);
static_assert(std::is_same_v<
              decltype(http3_origin_failure::request_committed), bool>);
static_assert(std::is_same_v<
              decltype(http3_origin_failure::request_method_safe), bool>);
static_assert(noexcept(
    std::declval<const http3_origin_failure &>().route_retry_permitted()));
static_assert(noexcept(std::declval<const http3_origin_failure &>()
                           .transport_fallback_permitted()));

[[maybe_unused]] bool route_retry_contracts() noexcept {
  const http3_origin_failure unsafe_method_before_commit{
      .kind = http3_origin_failure_kind::transport_connection,
      .status = STATUS_BAD_NETWORK_PATH,
      .request_committed = false,
      .request_method_safe = false};
  const http3_origin_failure committed_request{
      .kind = http3_origin_failure_kind::transport_timeout,
      .status = STATUS_IO_TIMEOUT,
      .request_committed = true,
      .request_method_safe = true};
  return unsafe_method_before_commit.route_retry_permitted() &&
         !unsafe_method_before_commit.transport_fallback_permitted() &&
         !committed_request.route_retry_permitted();
}
static_assert(std::is_same_v<
              decltype(http3_origin_exchange_options::
                           borrowed_request_cancellation_event),
              PKEVENT>);
static_assert(std::is_same_v<
              decltype(http3_origin_exchange_options::
                           borrowed_service_cancellation_event),
              PKEVENT>);
static_assert(std::is_same_v<
              decltype(http3_origin_result::peer_settings_received), bool>);
static_assert(std::is_same_v<
              decltype(http3_origin_result::
                           qpack_decoder_acknowledgement_queued),
              bool>);

// Compile the complete construction/cancellation/budget surface without
// loading a driver or creating a QUIC connection. This catches any future
// regression that makes construction throwing or collapses the independent
// request/service cancellation authorities back into one event.
[[maybe_unused]] NTSTATUS compile_origin_exchange_contract(
    ntl::net::http3::msquic_backend::connection_context configured,
    std::shared_ptr<peer_policy> policy, PKEVENT request_cancellation,
    PKEVENT service_cancellation) noexcept {
  http3_origin_allocation_budget budget(64 * 1024 * 1024);
  http3_origin_exchange_options options{
      .limits = {},
      .borrowed_allocation_budget = &budget,
      .borrowed_request_cancellation_event = request_cancellation,
      .borrowed_service_cancellation_event = service_cancellation};
  request message;
  auto value = exchange::try_create(
      std::move(configured), "origin.example", 443,
      std::move(message), std::move(policy), options);
  if (!value)
    return value.status();
  (*value)->cancel();

  const auto reservation = options.limits.allocation_reservation();
  if (!reservation || !budget.try_acquire(*reservation))
    return STATUS_QUOTA_EXCEEDED;
  budget.release(*reservation);
  return budget.current() == 0 && budget.peak() == *reservation
             ? STATUS_SUCCESS
             : STATUS_INTERNAL_ERROR;
}

} // namespace
