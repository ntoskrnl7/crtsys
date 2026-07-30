#pragma once

#include <cstddef>
#include <filesystem>

#include <ntl/wfp/management>

namespace crtsys::wfp_sample::browser_https {

struct browser_policy_diagnostic_summary {
  std::filesystem::path report_path;
  std::size_t ipv4_filter_count = 0;
  std::size_t ipv6_filter_count = 0;
  bool ipv4_inventory_truncated = false;
  bool ipv6_inventory_truncated = false;
};

/**
 * Verifies the exact fail-closed QUIC policy objects and writes a bounded
 * inventory of both ALE_AUTH_CONNECT layers. Application identifiers are
 * recorded only as byte counts and hashes.
 */
browser_policy_diagnostic_summary
verify_browser_quic_block_policy(
    const ntl::wfp::dynamic_session &session,
    const ntl::wfp::application_id &browser,
    const std::filesystem::path &log_directory);

} // namespace crtsys::wfp_sample::browser_https
