#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_policy.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ntl/wfp/connect_redirect>

namespace crtsys::wfp_kernel_browser_https {

namespace contract = wfp_kernel_browser_https_inspection;
namespace {

void require_policy(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error("kernel browser WFP policy: " +
                             std::string(message));
}

const ntl::wfp::policy_condition_diagnostic &require_condition(
    const ntl::wfp::policy_filter_diagnostic &filter, const GUID &field,
    std::string_view name) {
  const auto *condition = filter.find_condition(field);
  require_policy(condition != nullptr, std::string(name) + " is missing");
  require_policy(condition->match_type == FWP_MATCH_EQUAL,
                  std::string(name) + " is not exact");
  return *condition;
}

template <class Layer>
std::pair<std::uint64_t, std::uint16_t> verify_native_block(
    const ntl::wfp::policy_session &session,
    ntl::wfp::filter_key<Layer> key,
    const ntl::wfp::application_id &browser) {
  const auto filter = session.inspect_filter(key);
  const auto layer = session.inspect_layer<Layer>();
  require_policy(filter.has_value(), "native UDP/443 filter is missing");
  require_policy(layer.has_value(), "native UDP/443 layer is missing");
  require_policy(filter->filter_id != 0, "native filter ID is zero");
  require_policy(filter->action_type == FWP_ACTION_BLOCK,
                 "native UDP/443 action is not block");
  require_policy(filter->has_provider &&
                     contract::provider_key.matches(filter->provider_key),
                 "native UDP/443 provider does not match");
  require_policy(contract::sublayer_key.matches(filter->sublayer_key),
                 "native UDP/443 sublayer does not match");
  require_policy(!filter->conditions_truncated &&
                     filter->conditions.size() == 3,
                 "native UDP/443 conditions are not exact");

  const auto &application =
      require_condition(*filter, FWPM_CONDITION_ALE_APP_ID, "application ID");
  const auto expected_hash = contract::hash_application_id(
      browser.bytes().data(), browser.bytes().size());
  require_policy(application.value_type == FWP_BYTE_BLOB_TYPE &&
                     application.blob_size == browser.bytes().size() &&
                     application.blob_hash == expected_hash,
                 "native UDP/443 application ID does not match");
  const auto &protocol =
      require_condition(*filter, FWPM_CONDITION_IP_PROTOCOL, "protocol");
  require_policy(protocol.value_type == FWP_UINT8 &&
                     protocol.scalar_value == IPPROTO_UDP,
                 "native UDP/443 protocol is not UDP");
  const auto &port = require_condition(
      *filter, FWPM_CONDITION_IP_REMOTE_PORT, "remote port");
  require_policy(port.value_type == FWP_UINT16 && port.scalar_value == 443,
                 "native UDP/443 remote port is not 443");
  return {filter->filter_id, layer->layer_id};
}

} // namespace

native_quic_policy_evidence
install_browser_policy(ntl::wfp::policy_session &session,
                       const ntl::wfp::application_id &browser,
                       const contract::service_info &service) {
  if (!service.process_id || !service.tcp_ready ||
      !service.workspace_lifetime_passed || !service.tcp_port_v4 ||
      !service.tcp_port_v6)
    throw std::invalid_argument("kernel browser service is not TCP-ready");

  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key,
         L"crtsys kernel browser HTTPS inspection provider",
         L"Dynamic policy scoped to one explicitly selected browser"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key,
         L"crtsys kernel browser HTTPS inspection sublayer",
         L"Inspect selected-browser TCP 443 and prevent unchecked UDP 443",
         0x7fff});

    const auto redirect_callout_v4 =
        transaction.add_callout<contract::redirect_layer_v4>(
            provider,
            {contract::redirect_callout_key_v4,
             L"Kernel browser HTTPS IPv4 redirect", L""});
    const auto redirect_callout_v6 =
        transaction.add_callout<contract::redirect_layer_v6>(
            provider,
            {contract::redirect_callout_key_v6,
             L"Kernel browser HTTPS IPv6 redirect", L""});

    ntl::wfp::connect_redirect_filter_builder<contract::redirect_layer_v4>
        tcp_v4(contract::redirect_filter_key_v4,
               L"Inspect selected browser IPv4 HTTPS",
               {service.process_id, service.tcp_port_v4,
                ntl::wfp::original_destination_context::preserve},
               ntl::wfp::callout_unavailable::block);
    tcp_v4.description(
              L"Exact application ID, TCP, and destination port 443")
        .application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(sublayer, redirect_callout_v4,
                                             tcp_v4);

    ntl::wfp::connect_redirect_filter_builder<contract::redirect_layer_v6>
        tcp_v6(contract::redirect_filter_key_v6,
               L"Inspect selected browser IPv6 HTTPS",
               {service.process_id, service.tcp_port_v6,
                ntl::wfp::original_destination_context::preserve},
               ntl::wfp::callout_unavailable::block);
    tcp_v6.description(
              L"Exact application ID, TCP, and destination port 443")
        .application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(sublayer, redirect_callout_v6,
                                             tcp_v6);

    const auto quic_callout_v4 =
        transaction.add_callout<contract::quic_layer_v4>(
            provider,
            {contract::callout_key_v4,
             L"Kernel browser unchecked QUIC IPv4 gate", L""});
    const auto quic_callout_v6 =
        transaction.add_callout<contract::quic_layer_v6>(
            provider,
            {contract::callout_key_v6,
             L"Kernel browser unchecked QUIC IPv6 gate", L""});

    ntl::wfp::inspection_filter_builder<contract::quic_layer_v4> observe_v4(
        contract::filter_key_v4, L"Observe selected browser IPv4 UDP 443");
    observe_v4
        .description(L"Telemetry only; the adjacent native rule enforces block")
        .weight(0xff)
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_inspection_filter(sublayer, quic_callout_v4, observe_v4);

    ntl::wfp::inspection_filter_builder<contract::quic_layer_v6> observe_v6(
        contract::filter_key_v6, L"Observe selected browser IPv6 UDP 443");
    observe_v6
        .description(L"Telemetry only; the adjacent native rule enforces block")
        .weight(0xff)
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_inspection_filter(sublayer, quic_callout_v6, observe_v6);

    ntl::wfp::enforcement_filter_builder<contract::quic_layer_v4> block_v4(
        contract::enforcement_filter_key_v4,
        L"Hard-block selected browser IPv4 UDP 443",
        ntl::wfp::enforcement_action::block);
    block_v4.description(L"Native BFE block prevents unchecked QUIC bypass")
        .weight(0xff)
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_enforcement_filter(sublayer, block_v4);

    ntl::wfp::enforcement_filter_builder<contract::quic_layer_v6> block_v6(
        contract::enforcement_filter_key_v6,
        L"Hard-block selected browser IPv6 UDP 443",
        ntl::wfp::enforcement_action::block);
    block_v6.description(L"Native BFE block prevents unchecked QUIC bypass")
        .weight(0xff)
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_enforcement_filter(sublayer, block_v6);
  });

  native_quic_policy_evidence evidence{};
  evidence.application_id_hash = contract::hash_application_id(
      browser.bytes().data(), browser.bytes().size());
  const auto v4 = verify_native_block(
      session, contract::enforcement_filter_key_v4, browser);
  const auto v6 = verify_native_block(
      session, contract::enforcement_filter_key_v6, browser);
  evidence.filter_id_v4 = v4.first;
  evidence.layer_id_v4 = v4.second;
  evidence.filter_id_v6 = v6.first;
  evidence.layer_id_v6 = v6.second;
  return evidence;
}

void report_browser_policy_evidence(
    const native_quic_policy_evidence &evidence,
    const std::filesystem::path &log_directory) {
  const auto report = log_directory / L"wfp-policy-diagnostics.log";
  std::ofstream output(report, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create kernel WFP policy inventory");
  output << "schema=ntl-wfp-policy-diagnostics-v1 application-id-hash="
         << evidence.application_id_hash << '\n'
         << "filter layer=ale-auth-connect-v4 id=" << evidence.filter_id_v4
         << " action=FWP_ACTION_BLOCK protocol=17 remote-port=443 "
            "application-scoped=true\n"
         << "filter layer=ale-auth-connect-v6 id=" << evidence.filter_id_v6
         << " action=FWP_ACTION_BLOCK protocol=17 remote-port=443 "
            "application-scoped=true\n";
  if (!output)
    throw std::runtime_error("cannot write kernel WFP policy inventory");

  std::cout
      << "NTL WFP native UDP/443 block: verified kind=native-enforcement "
         "layer=ALE_AUTH_CONNECT_V4 action=FWP_ACTION_BLOCK protocol=UDP "
         "remote_port=443 application_scoped=true filter_id="
      << evidence.filter_id_v4 << '\n'
      << "NTL WFP native UDP/443 block: verified kind=native-enforcement "
         "layer=ALE_AUTH_CONNECT_V6 action=FWP_ACTION_BLOCK protocol=UDP "
         "remote_port=443 application_scoped=true filter_id="
      << evidence.filter_id_v6 << '\n';
}

} // namespace crtsys::wfp_kernel_browser_https
