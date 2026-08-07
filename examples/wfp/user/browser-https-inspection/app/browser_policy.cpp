#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_policy.hpp"

#include <stdexcept>

#include <ntl/wfp/all>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace contract = wfp_browser_https_inspection;

namespace {

template <class Layer>
void add_quic_fallback(ntl::wfp::policy_transaction &transaction,
                       const ntl::wfp::provider_ref &provider,
                       const ntl::wfp::sublayer_ref &sublayer,
                       ntl::wfp::terminating_callout_key<Layer> callout_key,
                       ntl::wfp::filter_key<Layer> filter_key,
                       ntl::wfp::filter_key<Layer> enforcement_key,
                       const ntl::wfp::application_id &application,
                       const wchar_t *family) {
  const auto callout = transaction.add_callout<Layer>(
      provider, {callout_key, std::wstring(L"Block selected browser ") + family +
                                  L" QUIC while no backend is active",
                 L"Typed fail-closed QUIC callout"});
  ntl::wfp::filter_builder<Layer> filter(
      filter_key, std::wstring(L"Block selected browser ") + family + L" QUIC",
      ntl::wfp::callout_unavailable::block);
  filter.application_equal(application)
      .protocol_equal(IPPROTO_UDP)
      .remote_port_equal(443);
  transaction.add_filter(sublayer, callout, filter);

  ntl::wfp::enforcement_filter_builder<Layer> enforcement(
      enforcement_key,
      std::wstring(L"Hard-block selected browser ") + family + L" QUIC",
      ntl::wfp::enforcement_action::block);
  enforcement.weight(0xff)
      .application_equal(application)
      .protocol_equal(IPPROTO_UDP)
      .remote_port_equal(443);
  transaction.add_enforcement_filter(sublayer, enforcement);
}

} // namespace

void install_browser_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &browser,
    std::uint16_t proxy_port_v4, std::uint16_t proxy_port_v6,
    std::optional<std::uint16_t> quic_proxy_port_v4,
    std::optional<std::uint16_t> quic_proxy_port_v6) {
  if (quic_proxy_port_v4.has_value() != quic_proxy_port_v6.has_value())
    throw std::invalid_argument(
        "IPv4 and IPv6 QUIC proxy targets must be enabled together");

  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key, L"crtsys browser HTTPS inspection provider",
         L"Dynamic policy for one explicitly selected browser"});
    const auto sublayer = transaction.add_sublayer(
        provider, {contract::sublayer_key,
                   L"crtsys browser HTTPS inspection sublayer",
                   L"Dual-stack TCP and QUIC inspection", 0x7500});

    const auto redirect_v4 = transaction.add_callout<contract::layer_v4>(
        provider, {contract::callout_key_v4, L"Redirect IPv4 HTTPS", L""});
    ntl::wfp::connect_redirect_filter_builder<contract::layer_v4> filter_v4(
        contract::filter_key_v4, L"Redirect selected browser IPv4 HTTPS",
        {::GetCurrentProcessId(), proxy_port_v4,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v4.application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(sublayer, redirect_v4, filter_v4);

    const auto redirect_v6 = transaction.add_callout<contract::layer_v6>(
        provider, {contract::callout_key_v6, L"Redirect IPv6 HTTPS", L""});
    ntl::wfp::connect_redirect_filter_builder<contract::layer_v6> filter_v6(
        contract::filter_key_v6, L"Redirect selected browser IPv6 HTTPS",
        {::GetCurrentProcessId(), proxy_port_v6,
         ntl::wfp::original_destination_context::preserve},
        ntl::wfp::callout_unavailable::block);
    filter_v6.application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(sublayer, redirect_v6, filter_v6);

    if (quic_proxy_port_v4) {
      if (*quic_proxy_port_v4 != *quic_proxy_port_v6)
        throw std::invalid_argument(
            "the owning UDP proxy uses one dual-stack local port");
      ntl::wfp::transparent_udp_proxy_policy::add_to(
          transaction, provider, sublayer, contract::udp_proxy_keys,
          {.application = browser,
           .original_port = 443,
           .local_proxy_port = *quic_proxy_port_v4});
      return;
    }

    add_quic_fallback(transaction, provider, sublayer, contract::quic_callout_key_v4,
                      contract::quic_filter_key_v4,
                      contract::quic_enforcement_filter_key_v4, browser, L"IPv4");
    add_quic_fallback(transaction, provider, sublayer, contract::quic_callout_key_v6,
                      contract::quic_filter_key_v6,
                      contract::quic_enforcement_filter_key_v6, browser, L"IPv6");
  });
}

void install_managed_http3_translation_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &client,
    std::uint16_t quic_proxy_port) {
  if (!quic_proxy_port)
    throw std::invalid_argument(
        "managed HTTP/3 translation requires a nonzero port");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {contract::provider_key, L"crtsys managed HTTP/3 provider",
         L"Dynamic provider for one managed QUIC client"});
    const auto sublayer = transaction.add_sublayer(
        provider, {contract::sublayer_key, L"crtsys managed HTTP/3 sublayer",
                   L"Dual-stack UDP 443 translation", 0x7500});
    ntl::wfp::transparent_udp_proxy_policy::add_to(
        transaction, provider, sublayer, contract::udp_proxy_keys,
        {.application = client,
         .original_port = 443,
         .local_proxy_port = quic_proxy_port});
  });
}

} // namespace crtsys::wfp_sample::browser_https
