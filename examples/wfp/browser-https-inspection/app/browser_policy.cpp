#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_policy.hpp"

#include <stdexcept>

#include <ntl/wfp/connect_redirect>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_sample::browser_https {

void install_browser_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &browser,
    std::uint16_t proxy_port_v4,
    std::uint16_t proxy_port_v6,
    std::optional<std::uint16_t> quic_proxy_port_v4,
    std::optional<std::uint16_t> quic_proxy_port_v6) {
  if (quic_proxy_port_v4.has_value() !=
      quic_proxy_port_v6.has_value())
    throw std::invalid_argument(
        "IPv4 and IPv6 QUIC redirect targets must be enabled together");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_browser_https_inspection::provider_key,
         L"crtsys NTL WFP browser HTTPS inspection provider",
         L"Dynamic provider for one explicitly selected browser"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_browser_https_inspection::sublayer_key,
         L"crtsys NTL WFP browser HTTPS inspection sublayer",
         L"Redirect the selected browser's IPv4 and IPv6 TCP port 443",
         0x7500});
    const auto callout_v4 =
        transaction.add_callout<
            wfp_browser_https_inspection::layer_v4>(
            provider,
            {wfp_browser_https_inspection::callout_key_v4,
             L"Redirect selected browser IPv4 HTTPS connections",
             L"Typed ALE_CONNECT_REDIRECT_V4 callout"});
    const auto callout_v6 =
        transaction.add_callout<
            wfp_browser_https_inspection::layer_v6>(
            provider,
            {wfp_browser_https_inspection::callout_key_v6,
             L"Redirect selected browser IPv6 HTTPS connections",
             L"Typed ALE_CONNECT_REDIRECT_V6 callout"});

    ntl::wfp::connect_redirect_filter_builder<
        wfp_browser_https_inspection::layer_v4>
        filter_v4(
            wfp_browser_https_inspection::filter_key_v4,
            L"Redirect one browser executable's IPv4 HTTPS",
            {::GetCurrentProcessId(), proxy_port_v4},
            ntl::wfp::callout_unavailable::permit);
    filter_v4.description(
                 L"Application ID, TCP, and remote port 443 are exact")
        .application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v4, filter_v4);

    ntl::wfp::connect_redirect_filter_builder<
        wfp_browser_https_inspection::layer_v6>
        filter_v6(
            wfp_browser_https_inspection::filter_key_v6,
            L"Redirect one browser executable's IPv6 HTTPS",
            {::GetCurrentProcessId(), proxy_port_v6},
            ntl::wfp::callout_unavailable::permit);
    filter_v6.description(
                 L"Application ID, TCP, and remote port 443 are exact")
        .application_equal(browser)
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(443);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v6, filter_v6);

    if (quic_proxy_port_v4 && quic_proxy_port_v6) {
      ntl::wfp::connect_redirect_filter_builder<
          wfp_browser_https_inspection::layer_v4>
          quic_redirect_v4(
              wfp_browser_https_inspection::
                  quic_redirect_filter_key_v4,
              L"Redirect one browser executable's IPv4 HTTP/3",
              {::GetCurrentProcessId(),
               *quic_proxy_port_v4},
              ntl::wfp::callout_unavailable::permit);
      quic_redirect_v4
          .description(
              L"Application ID, UDP, and remote port 443 are exact")
          .application_equal(browser)
          .protocol_equal(IPPROTO_UDP)
          .remote_port_equal(443);
      transaction.add_connect_redirect_filter(
          sublayer, callout_v4, quic_redirect_v4);

      ntl::wfp::connect_redirect_filter_builder<
          wfp_browser_https_inspection::layer_v6>
          quic_redirect_v6(
              wfp_browser_https_inspection::
                  quic_redirect_filter_key_v6,
              L"Redirect one browser executable's IPv6 HTTP/3",
              {::GetCurrentProcessId(),
               *quic_proxy_port_v6},
              ntl::wfp::callout_unavailable::permit);
      quic_redirect_v6
          .description(
              L"Application ID, UDP, and remote port 443 are exact")
          .application_equal(browser)
          .protocol_equal(IPPROTO_UDP)
          .remote_port_equal(443);
      transaction.add_connect_redirect_filter(
          sublayer, callout_v6, quic_redirect_v6);
      return;
    }

    const auto quic_callout_v4 =
        transaction.add_callout<
            wfp_browser_https_inspection::quic_layer_v4>(
            provider,
            {wfp_browser_https_inspection::quic_callout_key_v4,
             L"Block selected browser IPv4 QUIC while no backend is active",
             L"Typed ALE_AUTH_CONNECT_V4 fail-closed callout"});
    const auto quic_callout_v6 =
        transaction.add_callout<
            wfp_browser_https_inspection::quic_layer_v6>(
            provider,
            {wfp_browser_https_inspection::quic_callout_key_v6,
             L"Block selected browser IPv6 QUIC while no backend is active",
             L"Typed ALE_AUTH_CONNECT_V6 fail-closed callout"});

    ntl::wfp::filter_builder<
        wfp_browser_https_inspection::quic_layer_v4>
        quic_filter_v4(
            wfp_browser_https_inspection::quic_filter_key_v4,
            L"Block one browser executable's IPv4 QUIC fallback",
            ntl::wfp::callout_unavailable::block);
    quic_filter_v4.description(
                       L"Prevent UDP 443 from bypassing TLS inspection")
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_filter(
        sublayer, quic_callout_v4, quic_filter_v4);

    ntl::wfp::filter_builder<
        wfp_browser_https_inspection::quic_layer_v6>
        quic_filter_v6(
            wfp_browser_https_inspection::quic_filter_key_v6,
            L"Block one browser executable's IPv6 QUIC fallback",
            ntl::wfp::callout_unavailable::block);
    quic_filter_v6.description(
                       L"Prevent UDP 443 from bypassing TLS inspection")
        .application_equal(browser)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(443);
    transaction.add_filter(
        sublayer, quic_callout_v6, quic_filter_v6);
  });
}

void install_managed_http3_redirect_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &client,
    std::uint16_t quic_proxy_port) {
  if (quic_proxy_port == 0)
    throw std::invalid_argument(
        "managed HTTP/3 redirect requires a nonzero port");
  session.install(
      [&](ntl::wfp::policy_transaction &transaction) {
        const auto provider = transaction.add_provider(
            {wfp_browser_https_inspection::provider_key,
             L"crtsys NTL WFP managed HTTP/3 provider",
             L"Dynamic provider for one managed QUIC client"});
        const auto sublayer = transaction.add_sublayer(
            provider,
            {wfp_browser_https_inspection::sublayer_key,
             L"crtsys NTL WFP managed HTTP/3 sublayer",
             L"Redirect managed-client IPv4 and IPv6 UDP port 443",
             0x7500});
        const auto callout_v4 =
            transaction.add_callout<
                wfp_browser_https_inspection::layer_v4>(
                provider,
                {wfp_browser_https_inspection::callout_key_v4,
                 L"Redirect managed client IPv4 HTTP/3",
                 L"Typed ALE_CONNECT_REDIRECT_V4 UDP callout"});
        const auto callout_v6 =
            transaction.add_callout<
                wfp_browser_https_inspection::layer_v6>(
                provider,
                {wfp_browser_https_inspection::callout_key_v6,
                 L"Redirect managed client IPv6 HTTP/3",
                 L"Typed ALE_CONNECT_REDIRECT_V6 UDP callout"});

        ntl::wfp::connect_redirect_filter_builder<
            wfp_browser_https_inspection::layer_v4>
            filter_v4(
                wfp_browser_https_inspection::
                    quic_redirect_filter_key_v4,
                L"Redirect managed client IPv4 UDP 443",
                {::GetCurrentProcessId(), quic_proxy_port,
                 ntl::wfp::original_destination_context::omit},
                ntl::wfp::callout_unavailable::permit);
        filter_v4
            .description(
                L"Exact application, UDP, and remote port 443")
            .application_equal(client)
            .protocol_equal(IPPROTO_UDP)
            .remote_port_equal(443);
        transaction.add_connect_redirect_filter(
            sublayer, callout_v4, filter_v4);

        ntl::wfp::connect_redirect_filter_builder<
            wfp_browser_https_inspection::layer_v6>
            filter_v6(
                wfp_browser_https_inspection::
                    quic_redirect_filter_key_v6,
                L"Redirect managed client IPv6 UDP 443",
                {::GetCurrentProcessId(), quic_proxy_port,
                 ntl::wfp::original_destination_context::omit},
                ntl::wfp::callout_unavailable::permit);
        filter_v6
            .description(
                L"Exact application, UDP, and remote port 443")
            .application_equal(client)
            .protocol_equal(IPPROTO_UDP)
            .remote_port_equal(443);
        transaction.add_connect_redirect_filter(
            sublayer, callout_v6, filter_v6);
      });
}

} // namespace crtsys::wfp_sample::browser_https
