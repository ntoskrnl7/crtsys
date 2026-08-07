#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"

namespace {

template <class Layer>
ntl::wfp::terminating_decision redirect_selected_connection(
    ntl::wfp::connect_redirector &redirector,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto protocol =
      event.value(Layer::field::protocol).uint8();
  if (!protocol || *protocol != IPPROTO_TCP)
    return ntl::wfp::terminating_decision::block;

  const auto target =
      ntl::wfp::local_proxy_target::from_filter_context(
          event.filter().context());
  return redirector.redirect(event, target);
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto created =
      ntl::wfp::connect_redirector::try_create(
          wfp_connect_redirect::provider_key);
  if (!created)
    return created.status();

  auto redirector = std::make_shared<ntl::wfp::connect_redirector>(
      std::move(*created));
  ntl::wfp::callout_driver<> callouts(driver);

  const ntl::status registered = callouts.add_terminating(
      wfp_connect_redirect::callout_key, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<
             wfp_connect_redirect::layer_v4> &event) noexcept {
        return redirect_selected_connection(owned_redirector, event);
      });
  if (!registered.is_ok())
    return registered;

  const ntl::status registered_v6 = callouts.add_terminating(
      wfp_connect_redirect::callout_key_v6, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<
             wfp_connect_redirect::layer_v6> &event) noexcept {
        return redirect_selected_connection(owned_redirector, event);
      });
  if (!registered_v6.is_ok())
    return registered_v6;

  driver.on_unload([callouts] {
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
