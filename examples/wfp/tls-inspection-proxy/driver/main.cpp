#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "tls_inspection_proxy_contract.hpp"
namespace inspection_contract = wfp_tls_inspection_proxy;

namespace {

ntl::wfp::connect_redirector *g_redirector = nullptr;

constexpr auto redirect_selected_connection_v4 =
    +[](const ntl::wfp::classify_event<
           inspection_contract::layer_v4> &event) noexcept {
      auto *const redirector = g_redirector;
      const auto protocol =
          event.value(
                   inspection_contract::layer_v4::field::protocol)
              .uint8();
      if (!redirector || !protocol || *protocol != IPPROTO_TCP)
        return ntl::wfp::decision::block;

      const auto target =
          ntl::wfp::local_proxy_target::from_filter_context(
              event.filter().context());
      return redirector->redirect(event, target);
    };

constexpr auto redirect_selected_connection_v6 =
    +[](const ntl::wfp::classify_event<
           inspection_contract::layer_v6> &event) noexcept {
      auto *const redirector = g_redirector;
      const auto protocol =
          event.value(
                   inspection_contract::layer_v6::field::protocol)
              .uint8();
      if (!redirector || !protocol || *protocol != IPPROTO_TCP)
        return ntl::wfp::decision::block;

      const auto target =
          ntl::wfp::local_proxy_target::from_filter_context(
              event.filter().context());
      return redirector->redirect(event, target);
    };

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto created =
      ntl::wfp::connect_redirector::try_create(
          inspection_contract::provider_key);
  if (!created)
    return created.status();

  auto redirector = std::make_shared<ntl::wfp::connect_redirector>(
      std::move(*created));
  auto callouts =
      std::make_shared<ntl::wfp::callout_driver<>>(driver);

  g_redirector = redirector.get();
  const ntl::status registered_v4 =
      callouts->add<redirect_selected_connection_v4>(
          inspection_contract::callout_key_v4);
  if (!registered_v4.is_ok()) {
    g_redirector = nullptr;
    return registered_v4;
  }
  const ntl::status registered_v6 =
      callouts->add<redirect_selected_connection_v6>(
          inspection_contract::callout_key_v6);
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
