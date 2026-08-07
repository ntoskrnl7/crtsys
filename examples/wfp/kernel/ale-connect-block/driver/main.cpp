#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "ale_connect_block_contract.hpp"

namespace {

using connect_v4 = ntl::wfp::layers::ale_auth_connect_v4;

ntl::wfp::terminating_decision block_selected_connection(
    ntl::wfp::operational_telemetry &telemetry,
    const ntl::wfp::classify_event<connect_v4> &event) noexcept {
      telemetry.record_classify(connect_v4::runtime_id);
      // The management filter already limits invocation to TCP and the
      // selected port. Reading the typed values here also demonstrates that a
      // callback cannot accidentally use an index from another WFP layer.
      const auto protocol =
          event.value(connect_v4::field::protocol).uint8();
      const auto port =
          event.value(connect_v4::field::remote_port).uint16();
      if (!protocol || !port) {
        telemetry.record_permit(connect_v4::runtime_id);
        return ntl::wfp::terminating_decision::permit;
      }

      telemetry.record_block(connect_v4::runtime_id);
      return ntl::wfp::terminating_decision::block;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  ntl::wfp::callout_driver<> callouts(driver);
  auto telemetry =
      std::make_shared<ntl::wfp::operational_telemetry>();
  ntl::status result = callouts.watch_bfe(
      telemetry,
      [](ntl::wfp::operational_telemetry &owned_telemetry,
         FWPM_SERVICE_STATE state) noexcept {
        owned_telemetry.record_bfe_state(state);
      });
  if (!result.is_ok())
    return result;
  if (callouts.bfe_state() != FWPM_SERVICE_RUNNING)
    return STATUS_DEVICE_NOT_READY;
  result = callouts.add_terminating(
      wfp_ale_connect_block::callout_key, telemetry,
      [](ntl::wfp::operational_telemetry &owned_telemetry,
         const ntl::wfp::classify_event<connect_v4> &event) noexcept {
        return block_selected_connection(owned_telemetry, event);
      });
  if (!result.is_ok())
    return result;

  driver.on_unload([callouts, telemetry] {
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
    const auto final = telemetry->snapshot();
    NT_ASSERT(final.classify == final.blocked + final.permitted);
  });
  return ntl::status::ok();
}
