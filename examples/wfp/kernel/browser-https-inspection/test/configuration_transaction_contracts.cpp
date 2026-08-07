#include <ntddk.h>

#include <ntl/wfp/all>

#include "../driver/http3_service.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace {

using namespace crtsys::wfp_kernel_browser_https::driver;
using schannel_credentials = ntl::net::kernel::schannel_credentials;
using peer_policy = ntl::net::kernel::schannel_peer_certificate_policy;

// One owning H3 snapshot carries both transport configurations. There is only
// one peer-policy pointer, so strict QUIC and fallback Schannel cannot select
// different pin/mTLS policy instances after a replace/remove.
static_assert(!std::is_copy_constructible_v<http3_origin_security>);
static_assert(std::is_same_v<
              decltype(http3_origin_security::fallback_credentials),
              std::shared_ptr<schannel_credentials>>);
static_assert(std::is_same_v<decltype(http3_origin_security::policy),
                             std::shared_ptr<peer_policy>>);

// TCP updates are move-only rollback authorities. A failed second-leg H3
// publication cannot accidentally duplicate or discard the previous owning
// identity/origin state.
static_assert(
    !std::is_copy_constructible_v<tcp_service::identity_update>);
static_assert(
    std::is_nothrow_move_constructible_v<tcp_service::identity_update>);
static_assert(
    !std::is_copy_constructible_v<tcp_service::origin_security_update>);
static_assert(std::is_nothrow_move_constructible_v<
              tcp_service::origin_security_update>);
static_assert(std::is_same_v<
              decltype(std::declval<tcp_service &>()
                           .configuration_transaction_lock()),
              FAST_MUTEX &>);

// Compile the exact apply/rollback surface used by main.cpp. Runtime readers
// take configuration_transaction_lock before their local identity/security
// locks; writers hold that same lock across TCP apply, H3 publish and rollback.
[[maybe_unused]] NTSTATUS compile_identity_rollback(
    tcp_service &service,
    const wfp_kernel_browser_https_inspection::certificate_config &input)
    noexcept {
  auto update = service.configure(input);
  if (!update)
    return update.status();
  service.rollback(std::move(*update));
  return STATUS_SUCCESS;
}

[[maybe_unused]] NTSTATUS compile_origin_rollback(
    tcp_service &service,
    const wfp_kernel_browser_https_inspection::origin_security_config &input)
    noexcept {
  auto update = service.configure_origin_security(input);
  if (!update)
    return update.status();
  service.rollback(std::move(*update));
  return STATUS_SUCCESS;
}

} // namespace
