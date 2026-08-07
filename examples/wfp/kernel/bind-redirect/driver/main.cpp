#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "bind_redirect_contract.hpp"

namespace {

bool is_udp(
    const ntl::wfp::classify_event<
        wfp_bind_redirect::layer_v4> &event) noexcept {
  const auto protocol =
      event.value(wfp_bind_redirect::layer_v4::field::protocol)
          .uint8();
  return protocol && *protocol == IPPROTO_UDP;
}

bool is_udp(
    const ntl::wfp::classify_event<
        wfp_bind_redirect::layer_v6> &event) noexcept {
  const auto protocol =
      event.value(wfp_bind_redirect::layer_v6::field::protocol)
          .uint8();
  return protocol && *protocol == IPPROTO_UDP;
}

constexpr auto redirect_ipv4_bind =
    +[](const ntl::wfp::classify_event<
           wfp_bind_redirect::layer_v4> &event) noexcept {
      const auto selector =
          ntl::wfp::bind_redirect_selector::from_filter_context(
              event.filter().context());
      if (!is_udp(event) ||
          selector.value !=
              wfp_bind_redirect::selector_v4.value)
        return ntl::wfp::terminating_decision::block;

      return ntl::wfp::bind_redirector::redirect(
          event,
          ntl::wfp::local_bind_target_v4{
              .address = INADDR_LOOPBACK,
              .port = wfp_bind_redirect::redirected_port_v4});
    };

constexpr auto redirect_ipv6_bind =
    +[](const ntl::wfp::classify_event<
           wfp_bind_redirect::layer_v6> &event) noexcept {
      const auto selector =
          ntl::wfp::bind_redirect_selector::from_filter_context(
              event.filter().context());
      if (!is_udp(event) ||
          selector.value !=
              wfp_bind_redirect::selector_v6.value)
        return ntl::wfp::terminating_decision::block;

      ntl::wfp::local_bind_target_v6 target{};
      target.address.back() = 1;
      target.port = wfp_bind_redirect::redirected_port_v6;
      return ntl::wfp::bind_redirector::redirect(event, target);
    };

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  ntl::wfp::callout_driver<> callouts(driver);
  ntl::status registered = callouts.add_terminating(
      wfp_bind_redirect::callout_key_v4, redirect_ipv4_bind);
  if (!registered.is_ok())
    return registered;
  registered = callouts.add_terminating(
      wfp_bind_redirect::callout_key_v6, redirect_ipv6_bind);
  if (!registered.is_ok())
    return registered;

  driver.on_unload([callouts] {
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
