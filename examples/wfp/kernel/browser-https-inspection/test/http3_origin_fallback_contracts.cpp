#include <ntl/net/kernel/http_origin_fallback>

#include <type_traits>
#include <utility>

namespace {

using namespace ntl::net::kernel;
using fallback_exchange = kernel_http_origin_fallback_exchange;
using provider = ntl::net::kernel::wsk_provider;
using credentials = ntl::net::kernel::schannel_credentials;
using peer_policy =
    ntl::net::kernel::schannel_peer_certificate_policy;

static_assert(!std::is_copy_constructible_v<http3_origin_attempt>);
static_assert(std::is_nothrow_move_constructible_v<http3_origin_attempt>);
static_assert(!std::is_default_constructible_v<http3_origin_attempt>);
static_assert(!std::is_copy_constructible_v<fallback_exchange>);
static_assert(noexcept(std::declval<fallback_exchange &>().cancel()));
static_assert(std::is_same_v<
              decltype(http_origin_fallback_options::
                           borrowed_request_cancellation_event),
              PKEVENT>);
static_assert(std::is_same_v<
              decltype(http_origin_fallback_options::
                           borrowed_service_cancellation_event),
              PKEVENT>);
static_assert(noexcept(fallback_exchange::try_create(
    std::declval<std::shared_ptr<provider>>(),
    std::declval<std::string_view>(),
    std::declval<std::uint16_t>(),
    std::declval<http3_origin_attempt &&>(),
    std::declval<credentials>(),
    std::declval<std::shared_ptr<peer_policy>>(),
    std::declval<http_origin_fallback_options>())));

// The proof cannot be fabricated by ordinary callers.  This function only
// accepts one produced by kernel_http3_origin_exchange::run_classified().
[[maybe_unused]] NTSTATUS compile_fallback_contract(
    std::shared_ptr<provider> wsk, http3_origin_attempt &&strict_attempt,
    credentials client_credentials,
    std::shared_ptr<peer_policy> policy,
    http3_origin_allocation_budget &budget,
    PKEVENT request_cancellation,
    PKEVENT service_cancellation) noexcept {
  http_origin_fallback_options options{
      .limits = {},
      .borrowed_allocation_budget = &budget,
      .borrowed_request_cancellation_event = request_cancellation,
      .borrowed_service_cancellation_event = service_cancellation};
  auto fallback = fallback_exchange::try_create(
      std::move(wsk), "origin.example", 443, std::move(strict_attempt),
      std::move(client_credentials), std::move(policy), options);
  if (!fallback)
    return fallback.status();
  (*fallback)->cancel();
  return STATUS_SUCCESS;
}

} // namespace
