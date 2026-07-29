#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"

namespace {

ntl::wfp::connect_redirector *g_redirector = nullptr;

template <class Layer>
ntl::wfp::decision redirect_selected_connection(
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  auto *const redirector = g_redirector;
  const auto protocol =
      event.value(Layer::field::protocol).uint8();
  if (!redirector || !protocol || *protocol != IPPROTO_TCP)
    return ntl::wfp::decision::block;

  const auto target =
      ntl::wfp::local_proxy_target::from_filter_context(
          event.filter().context());
  return redirector->redirect(event, target);
}

constexpr auto redirect_selected_connection_v4 =
    +[](const ntl::wfp::classify_event<
           wfp_connect_redirect::layer_v4> &event) noexcept {
      return redirect_selected_connection(event);
    };

constexpr auto redirect_selected_connection_v6 =
    +[](const ntl::wfp::classify_event<
           wfp_connect_redirect::layer_v6> &event) noexcept {
      return redirect_selected_connection(event);
    };

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto created =
      ntl::wfp::connect_redirector::try_create(
          wfp_connect_redirect::provider_key);
  if (!created)
    return created.status();

  auto redirector = std::make_shared<ntl::wfp::connect_redirector>(
      std::move(*created));
  auto callouts =
      std::make_shared<ntl::wfp::callout_driver<>>(driver);

  g_redirector = redirector.get();
  const ntl::status registered =
      callouts->add<redirect_selected_connection_v4>(
          wfp_connect_redirect::callout_key);
  if (!registered.is_ok()) {
    g_redirector = nullptr;
    return registered;
  }
  const ntl::status registered_v6 =
      callouts->add<redirect_selected_connection_v6>(
          wfp_connect_redirect::callout_key_v6);
  if (!registered_v6.is_ok()) {
    (void)callouts->reset();
    g_redirector = nullptr;
    return registered_v6;
  }

  driver.on_unload([callouts, redirector] {
    const ntl::status result = callouts->reset();
    NT_ASSERT(result.is_ok());
    g_redirector = nullptr;
    redirector->reset();
  });
  return ntl::status::ok();
}
