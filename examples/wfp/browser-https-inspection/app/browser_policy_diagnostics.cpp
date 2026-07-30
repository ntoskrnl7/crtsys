#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_policy_diagnostics.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

void require_policy(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(
        "WFP policy diagnostic failed: " + std::string(message));
}

std::string guid_string(const GUID &value) {
  std::ostringstream output;
  output << '{' << std::hex << std::setfill('0')
         << std::setw(8) << value.Data1 << '-'
         << std::setw(4) << value.Data2 << '-'
         << std::setw(4) << value.Data3 << '-'
         << std::setw(2)
         << static_cast<unsigned>(value.Data4[0])
         << std::setw(2)
         << static_cast<unsigned>(value.Data4[1])
         << '-';
  for (std::size_t index = 2; index != 8; ++index)
    output << std::setw(2)
           << static_cast<unsigned>(value.Data4[index]);
  output << '}';
  return output.str();
}

const ntl::wfp::policy_condition_diagnostic &
require_condition(
    const ntl::wfp::policy_filter_diagnostic &filter,
    const GUID &field, std::string_view name) {
  const auto *condition = filter.find_condition(field);
  require_policy(condition != nullptr,
                 std::string(name) + " condition is missing");
  require_policy(condition->match_type == FWP_MATCH_EQUAL,
                 std::string(name) + " condition is not exact");
  return *condition;
}

template <class Layer>
void verify_quic_filter(
    const ntl::wfp::policy_filter_diagnostic &filter,
    ntl::wfp::callout_key<Layer> expected_callout,
    const ntl::wfp::application_id &browser) {
  require_policy(
      ::IsEqualGUID(filter.layer_key, Layer::policy_key()),
      "filter is attached to the wrong layer");
  require_policy(filter.has_provider, "filter has no provider");
  require_policy(
      wfp_browser_https_inspection::provider_key.matches(
          filter.provider_key),
      "filter provider key does not match");
  require_policy(
      wfp_browser_https_inspection::sublayer_key.matches(
          filter.sublayer_key),
      "filter sublayer key does not match");
  require_policy(filter.sublayer_weight == 0x7500,
                 "filter sublayer weight does not match");
  require_policy(
      filter.action_type == FWP_ACTION_CALLOUT_TERMINATING,
      "filter action is not a terminating callout");
  require_policy(
      expected_callout.matches(filter.action_callout_key),
      "filter callout key does not match");
  require_policy(
      (filter.flags & FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT) != 0,
      "filter does not clear lower-priority action rights");
  require_policy(!filter.conditions_truncated,
                 "filter condition snapshot was truncated");
  require_policy(filter.conditions.size() == 3,
                 "filter condition count is not exactly three");

  const auto &application = require_condition(
      filter, FWPM_CONDITION_ALE_APP_ID, "application ID");
  require_policy(application.value_type == FWP_BYTE_BLOB_TYPE,
                 "application ID condition has the wrong type");
  require_policy(application.blob_size == browser.bytes().size(),
                 "application ID condition has the wrong size");
  require_policy(
      application.blob_hash ==
          wfp_browser_https_inspection::hash_application_id(
              browser.bytes().data(), browser.bytes().size()),
      "application ID condition has the wrong value");

  const auto &protocol = require_condition(
      filter, FWPM_CONDITION_IP_PROTOCOL, "protocol");
  require_policy(protocol.value_type == FWP_UINT8,
                 "protocol condition has the wrong type");
  require_policy(protocol.scalar_value == IPPROTO_UDP,
                 "protocol condition is not UDP");

  const auto &port = require_condition(
      filter, FWPM_CONDITION_IP_REMOTE_PORT, "remote port");
  require_policy(port.value_type == FWP_UINT16,
                 "remote port condition has the wrong type");
  require_policy(port.scalar_value == 443,
                 "remote port condition is not 443");
}

void write_inventory(
    std::ofstream &output, std::string_view name,
    const ntl::wfp::policy_filter_inventory &inventory) {
  output << "layer=" << name
         << " count=" << inventory.filters.size()
         << " truncated=" << (inventory.truncated ? "yes" : "no")
         << '\n';
  for (std::size_t index = 0;
       index != inventory.filters.size(); ++index) {
    const auto &filter = inventory.filters[index];
    output << "filter layer=" << name
           << " index=" << index
           << " id=" << filter.filter_id
           << " key=" << guid_string(filter.filter_key)
           << " provider="
           << (filter.has_provider
                   ? guid_string(filter.provider_key)
                   : std::string("none"))
           << " sublayer=" << guid_string(filter.sublayer_key)
           << " sublayer-weight=" << filter.sublayer_weight
           << " weight-type="
           << static_cast<unsigned>(filter.weight_type)
           << " weight=" << filter.weight_value
           << " effective-weight-type="
           << static_cast<unsigned>(
                  filter.effective_weight_type)
           << " effective-weight="
           << filter.effective_weight_value
           << " action=" << filter.action_type
           << " flags=" << filter.flags
           << " conditions=" << filter.conditions.size()
           << " conditions-truncated="
           << (filter.conditions_truncated ? "yes" : "no")
           << '\n';
    for (const auto &condition : filter.conditions) {
      output << "condition filter-id=" << filter.filter_id
             << " field=" << guid_string(condition.field_key)
             << " match="
             << static_cast<unsigned>(condition.match_type)
             << " type="
             << static_cast<unsigned>(condition.value_type)
             << " scalar=" << condition.scalar_value
             << " blob-size=" << condition.blob_size
             << " blob-hash=" << condition.blob_hash
             << '\n';
    }
  }
}

} // namespace

browser_policy_diagnostic_summary
verify_browser_quic_block_policy(
    const ntl::wfp::dynamic_session &session,
    const ntl::wfp::application_id &browser,
    const std::filesystem::path &log_directory) {
  require_policy(
      session.provider_present(
          wfp_browser_https_inspection::provider_key),
      "provider is missing");
  require_policy(
      session.sublayer_present(
          wfp_browser_https_inspection::sublayer_key),
      "sublayer is missing");
  require_policy(
      session.callout_present(
          wfp_browser_https_inspection::quic_callout_key_v4),
      "IPv4 QUIC callout is missing or on the wrong layer");
  require_policy(
      session.callout_present(
          wfp_browser_https_inspection::quic_callout_key_v6),
      "IPv6 QUIC callout is missing or on the wrong layer");

  const auto filter_v4 = session.inspect_filter(
      wfp_browser_https_inspection::quic_filter_key_v4);
  const auto filter_v6 = session.inspect_filter(
      wfp_browser_https_inspection::quic_filter_key_v6);
  require_policy(filter_v4.has_value(),
                 "IPv4 QUIC filter is missing");
  require_policy(filter_v6.has_value(),
                 "IPv6 QUIC filter is missing");
  verify_quic_filter(
      *filter_v4,
      wfp_browser_https_inspection::quic_callout_key_v4,
      browser);
  verify_quic_filter(
      *filter_v6,
      wfp_browser_https_inspection::quic_callout_key_v6,
      browser);

  constexpr std::size_t inventory_bound = 256;
  const auto inventory_v4 =
      session.enumerate_filters<
          wfp_browser_https_inspection::quic_layer_v4>(
          inventory_bound);
  const auto inventory_v6 =
      session.enumerate_filters<
          wfp_browser_https_inspection::quic_layer_v6>(
          inventory_bound);

  browser_policy_diagnostic_summary summary{};
  summary.report_path =
      log_directory / L"wfp-policy-diagnostics.log";
  summary.ipv4_filter_count = inventory_v4.filters.size();
  summary.ipv6_filter_count = inventory_v6.filters.size();
  summary.ipv4_inventory_truncated = inventory_v4.truncated;
  summary.ipv6_inventory_truncated = inventory_v6.truncated;

  std::ofstream output(
      summary.report_path,
      std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error(
        "cannot create WFP policy diagnostics report");
  output << "schema=ntl-wfp-policy-diagnostics-v1"
         << " application-id-size=" << browser.bytes().size()
         << " application-id-hash="
         << wfp_browser_https_inspection::hash_application_id(
                browser.bytes().data(), browser.bytes().size())
         << " inventory-bound=" << inventory_bound << '\n';
  write_inventory(output, "ale-auth-connect-v4", inventory_v4);
  write_inventory(output, "ale-auth-connect-v6", inventory_v6);
  if (!output)
    throw std::runtime_error(
        "cannot write WFP policy diagnostics report");
  return summary;
}

} // namespace crtsys::wfp_sample::browser_https
