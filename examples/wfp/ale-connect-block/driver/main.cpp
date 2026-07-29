#include <ntddk.h>

#include <memory>

#include <ntl/driver>
#include <ntl/wfp/all>

#include "ale_connect_block_contract.hpp"

namespace {

using connect_v4 = ntl::wfp::layers::ale_auth_connect_v4;

constexpr auto block_selected_connection =
    +[](const ntl::wfp::classify_event<connect_v4> &event) noexcept {
      // The management filter already limits invocation to TCP and the
      // selected port. Reading the typed values here also demonstrates that a
      // callback cannot accidentally use an index from another WFP layer.
      const auto protocol =
          event.value(connect_v4::field::protocol).uint8();
      const auto port =
          event.value(connect_v4::field::remote_port).uint16();
      if (!protocol || !port)
        return ntl::wfp::decision::continue_classification;

      return ntl::wfp::decision::block;
    };

bool validate_decision_contract() noexcept {
  FWPS_FILTER2 clear_after_permit{};
  clear_after_permit.action.type = FWP_ACTION_CALLOUT_TERMINATING;
  clear_after_permit.flags = FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT;

  FWPS_CLASSIFY_OUT0 output{};
  output.actionType = FWP_ACTION_BLOCK;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::permit, &clear_after_permit, output);
  if (output.actionType != FWP_ACTION_BLOCK)
    return false;

  output = {};
  output.actionType = FWP_ACTION_PERMIT;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::block, nullptr, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0)
    return false;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::permit, &clear_after_permit, output);
  if (output.actionType != FWP_ACTION_PERMIT ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0)
    return false;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::block_and_absorb, nullptr, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      (output.rights & FWPS_RIGHT_ACTION_WRITE) != 0 ||
      (output.flags & FWPS_CLASSIFY_OUT_FLAG_ABSORB) == 0)
    return false;

  FWPS_FILTER2 inspection{};
  inspection.action.type = FWP_ACTION_CALLOUT_INSPECTION;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::block, &inspection, output);
  if (output.actionType != FWP_ACTION_CONTINUE)
    return false;

  FWPS_FILTER2 terminating{};
  terminating.action.type = FWP_ACTION_CALLOUT_TERMINATING;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail::apply_decision(
      ntl::wfp::decision::continue_classification, &terminating, output);
  return output.actionType == FWP_ACTION_BLOCK &&
         (output.rights & FWPS_RIGHT_ACTION_WRITE) == 0;
}

bool validate_stream_contract() noexcept {
  FWPS_STREAM_DATA0 data{};
  data.dataLength = 12;
  FWPS_STREAM_CALLOUT_IO_PACKET0 packet{};
  packet.streamData = &data;

  FWPS_CLASSIFY_OUT0 output{};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::need_more(32), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_NONE ||
      packet.streamAction != FWPS_STREAM_ACTION_NEED_MORE_DATA ||
      packet.countBytesRequired != 32)
    return false;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::permit(99), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_PERMIT ||
      packet.streamAction != FWPS_STREAM_ACTION_NONE ||
      packet.countBytesEnforced != data.dataLength)
    return false;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::defer(), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_BLOCK ||
      packet.streamAction != FWPS_STREAM_ACTION_NONE ||
      packet.countBytesEnforced != data.dataLength)
    return false;

  data.flags = FWPS_STREAM_FLAG_RECEIVE;
  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::defer(), nullptr, &packet, output);
  if (output.actionType != FWP_ACTION_NONE ||
      packet.streamAction != FWPS_STREAM_ACTION_DEFER)
    return false;

  output = {};
  output.rights = FWPS_RIGHT_ACTION_WRITE;
  output.flags = FWPS_CLASSIFY_OUT_FLAG_BUFFER_LIMIT_REACHED;
  ntl::wfp::detail_apply_stream_result(
      ntl::wfp::stream_result::need_more(32), nullptr, &packet, output);
  return output.actionType == FWP_ACTION_BLOCK &&
         packet.streamAction == FWPS_STREAM_ACTION_NONE &&
         packet.countBytesEnforced == data.dataLength;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  if (!validate_decision_contract() || !validate_stream_contract())
    return STATUS_ASSERTION_FAILURE;

  auto owned_callouts =
      std::make_shared<ntl::wfp::callout_driver<>>(driver);
  const ntl::status result =
      owned_callouts->add<block_selected_connection>(
          wfp_ale_connect_block::callout_key);
  if (!result.is_ok())
    return result;

  driver.on_unload([owned_callouts] {
    const ntl::status result = owned_callouts->reset();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
