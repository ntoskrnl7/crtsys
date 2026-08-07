#pragma once

#include <cstdint>
#include <optional>

#include <ntl/wfp/management>

namespace crtsys::wfp_sample::browser_https {

void install_browser_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &browser,
    std::uint16_t proxy_port_v4,
    std::uint16_t proxy_port_v6,
    std::optional<std::uint16_t> quic_proxy_port_v4 =
        std::nullopt,
    std::optional<std::uint16_t> quic_proxy_port_v6 =
        std::nullopt);

/**
 * Installs bounded dual-stack UDP/443 tuple translation for one managed
 * client. The caller must own the client trust decision and the local QUIC
 * endpoint; ordinary browsers should use install_browser_policy()'s
 * fail-closed TCP fallback unless those requirements are met.
 */
void install_managed_http3_translation_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &client,
    std::uint16_t quic_proxy_port);

} // namespace crtsys::wfp_sample::browser_https
