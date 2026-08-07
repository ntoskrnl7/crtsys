#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "managed_policy.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ntl/wfp/all>

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;

void require_policy(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error("kernel browser managed WFP policy: " +
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

} // namespace

managed_transport_policy_evidence install_managed_http3_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    const contract::service_info &service, std::uint16_t original_port) {
  if (!service.process_id || !service.workspace_lifetime_passed ||
      !service.http3_ready || !service.http3_port ||
      original_port == 0)
    throw std::invalid_argument("kernel managed HTTP/3 service is not ready");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key,
         L"crtsys kernel managed HTTP/3 acceptance provider",
         L"Translates only this controlled acceptance executable"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key,
         L"crtsys kernel managed HTTP/3 acceptance sublayer",
         L"Scoped dual-stack connectionless UDP translation", 0x7fff});
    ntl::wfp::transparent_udp_proxy_policy::add_to(
        transaction, provider, sublayer, contract::udp_proxy_keys,
        {.application = application,
         .original_port = original_port,
         .local_proxy_port = service.http3_port});
  });

  managed_transport_policy_evidence evidence{};
  evidence.application_id_hash = contract::hash_application_id(
      application.bytes().data(), application.bytes().size());
  const auto verify_flow = [&](auto key) {
    const auto filter = session.inspect_filter(key);
    require_policy(filter.has_value(),
                    "managed HTTP/3 flow filter is missing");
    require_policy(filter->filter_id != 0 &&
                       filter->action_type == FWP_ACTION_CALLOUT_UNKNOWN,
                   "managed HTTP/3 flow action is invalid");
    require_policy(!filter->conditions_truncated &&
                       filter->conditions.size() == 4,
                   "managed HTTP/3 conditions are incomplete");
    const auto &protocol = require_condition(
        *filter, FWPM_CONDITION_IP_PROTOCOL, "managed protocol");
    const auto &port = require_condition(
        *filter, FWPM_CONDITION_IP_REMOTE_PORT, "managed port");
    require_policy(protocol.scalar_value == IPPROTO_UDP &&
                       port.scalar_value == original_port,
                   "managed HTTP/3 tuple is invalid");
    return filter->filter_id;
  };
  evidence.http3_filter_id_v4 =
      verify_flow(contract::managed_http3_flow_filter_key_v4);
  evidence.http3_filter_id_v6 =
      verify_flow(contract::managed_http3_flow_filter_key_v6);
  const auto verify_datagram = [&](auto key) {
    const auto filter = session.inspect_filter(key);
    require_policy(filter.has_value(),
                   "managed HTTP/3 datagram filter is missing");
    require_policy(
        filter->filter_id != 0 &&
            filter->action_type == FWP_ACTION_CALLOUT_TERMINATING &&
            (filter->flags & FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT) != 0,
        "managed HTTP/3 datagram action is invalid");
    require_policy(!filter->conditions_truncated &&
                       filter->conditions.size() == 3,
                   "managed HTTP/3 datagram conditions are incomplete");
    const auto &protocol = require_condition(
        *filter, FWPM_CONDITION_IP_PROTOCOL, "managed datagram protocol");
    const auto &direction = require_condition(
        *filter, FWPM_CONDITION_DIRECTION, "managed datagram direction");
    const auto &port = require_condition(
        *filter, FWPM_CONDITION_IP_REMOTE_PORT, "managed datagram port");
    require_policy(protocol.scalar_value == IPPROTO_UDP &&
                       direction.scalar_value == FWP_DIRECTION_OUTBOUND &&
                       port.scalar_value == original_port,
                   "managed HTTP/3 datagram tuple is invalid");
    return filter->filter_id;
  };
  const auto verify_reverse = [&](auto key) {
    const auto filter = session.inspect_filter(key);
    require_policy(filter.has_value(),
                   "managed HTTP/3 reverse filter is missing");
    require_policy(
        filter->filter_id != 0 &&
            filter->action_type == FWP_ACTION_CALLOUT_TERMINATING &&
            (filter->flags & FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT) != 0,
        "managed HTTP/3 reverse action is invalid");
    require_policy(!filter->conditions_truncated &&
                       filter->conditions.size() == 1,
                   "managed HTTP/3 reverse conditions are incomplete");
    const auto &address = require_condition(
        *filter, FWPM_CONDITION_IP_LOCAL_ADDRESS,
        "managed reverse loopback address");
    require_policy(address.match_type == FWP_MATCH_EQUAL,
                   "managed HTTP/3 reverse address match is invalid");
    return filter->filter_id;
  };
  evidence.datagram_filter_id_v4 =
      verify_datagram(contract::managed_http3_outbound_filter_key_v4);
  evidence.datagram_filter_id_v6 =
      verify_datagram(contract::managed_http3_outbound_filter_key_v6);
  evidence.reverse_filter_id_v4 =
      verify_reverse(contract::managed_http3_reverse_filter_key_v4);
  evidence.reverse_filter_id_v6 =
      verify_reverse(contract::managed_http3_reverse_filter_key_v6);
  return evidence;
}

managed_tcp_policy_evidence install_managed_tcp_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    const contract::service_info &service, std::uint16_t original_port_v4,
    std::uint16_t original_port_v6) {
  if (!service.process_id || !service.tcp_ready ||
      !service.workspace_lifetime_passed || !service.tcp_port_v4 ||
      !service.tcp_port_v6 || !original_port_v4 || !original_port_v6)
    throw std::invalid_argument("kernel managed TCP service is not ready");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key,
         L"crtsys kernel managed TCP acceptance provider",
         L"Redirects only this controlled acceptance executable"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {contract::sublayer_key,
         L"crtsys kernel managed TCP acceptance sublayer",
         L"Scoped dual-stack TCP original-destination acceptance", 0x7fff});
    const auto callout_v4 =
        transaction.add_callout<contract::redirect_layer_v4>(
            provider, {contract::redirect_callout_key_v4,
                       L"Managed HTTPS IPv4 redirect", L""});
    const auto callout_v6 =
        transaction.add_callout<contract::redirect_layer_v6>(
            provider, {contract::redirect_callout_key_v6,
                       L"Managed HTTPS IPv6 redirect", L""});
    ntl::wfp::connect_redirect_filter_builder<contract::redirect_layer_v4>
        ipv4(contract::redirect_filter_key_v4,
             L"Redirect managed IPv4 HTTPS to kernel",
             {service.process_id, service.tcp_port_v4,
              ntl::wfp::original_destination_context::preserve},
             ntl::wfp::callout_unavailable::block);
    ipv4.application_equal(application)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v4);
    transaction.add_connect_redirect_filter(sublayer, callout_v4, ipv4);
    ntl::wfp::connect_redirect_filter_builder<contract::redirect_layer_v6>
        ipv6(contract::redirect_filter_key_v6,
             L"Redirect managed IPv6 HTTPS to kernel",
             {service.process_id, service.tcp_port_v6,
              ntl::wfp::original_destination_context::preserve},
             ntl::wfp::callout_unavailable::block);
    ipv6.application_equal(application)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v6);
    transaction.add_connect_redirect_filter(sublayer, callout_v6, ipv6);
  });
  managed_tcp_policy_evidence evidence{};
  evidence.application_id_hash = contract::hash_application_id(
      application.bytes().data(), application.bytes().size());
  const auto v4 = session.inspect_filter(contract::redirect_filter_key_v4);
  const auto v6 = session.inspect_filter(contract::redirect_filter_key_v6);
  require_policy(v4.has_value() && v6.has_value(),
                 "managed TCP redirect filters are missing");
  require_policy(v4->action_type == FWP_ACTION_CALLOUT_TERMINATING &&
                     v6->action_type == FWP_ACTION_CALLOUT_TERMINATING,
                 "managed TCP redirect action is invalid");
  evidence.filter_id_v4 = v4->filter_id;
  evidence.filter_id_v6 = v6->filter_id;
  return evidence;
}

} // namespace crtsys::wfp_kernel_browser_https
