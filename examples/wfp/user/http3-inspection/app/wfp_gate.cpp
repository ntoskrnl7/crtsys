#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "wfp_gate.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace crtsys::wfp_user_http3 {
namespace {

namespace contract = wfp_user_http3_inspection;

[[noreturn]] void throw_windows(const char *operation) {
  throw std::system_error(
      ::GetLastError(), std::system_category(), operation);
}

const contract::layer_telemetry &layer_for(
    const contract::gate_telemetry &value, int family) {
  if (family == AF_INET)
    return value.ipv4;
  if (family == AF_INET6)
    return value.ipv6;
  throw std::invalid_argument("unsupported WFP gate address family");
}

bool counters_equal(
    const contract::layer_telemetry &left,
    const contract::layer_telemetry &right) noexcept {
  return left.classify_hits == right.classify_hits &&
         left.permit_decisions == right.permit_decisions &&
         left.invalid_protocol == right.invalid_protocol &&
         left.action_write_available == right.action_write_available &&
         left.action_write_missing == right.action_write_missing;
}

bool counters_equal(
    const contract::gate_telemetry &left,
    const contract::gate_telemetry &right) noexcept {
  return counters_equal(left.ipv4, right.ipv4) &&
         counters_equal(left.ipv6, right.ipv6);
}

bool captured_loopback(
    const contract::layer_telemetry &value, int family) noexcept {
  if (family == AF_INET) {
    return value.last_remote_address_v4 == INADDR_LOOPBACK ||
           value.last_remote_address_v4 == htonl(INADDR_LOOPBACK);
  }
  IN6_ADDR address{};
  static_assert(sizeof(address) == sizeof(value.last_remote_address_v6));
  std::memcpy(
      &address, value.last_remote_address_v6, sizeof(address));
  return IN6_IS_ADDR_LOOPBACK(&address) != 0;
}

} // namespace

gate_policy::gate_policy(
    ntl::wfp::policy_session session,
    contract::gate_telemetry before,
    std::uint16_t port, bool unavailable) noexcept
    : session_(std::move(session)), before_(before), port_(port),
      unavailable_(unavailable) {}

gate_controller::gate_controller(
    const std::filesystem::path &controlled_application,
    std::uint32_t controlled_process_id)
    : application_(ntl::wfp::application_id::from_path(
          controlled_application.wstring())),
      application_hash_(contract::hash_application_id(
          application_.bytes().data(), application_.bytes().size())),
      process_id_(controlled_process_id) {
  device_ = ::CreateFileW(
      contract::user_device_path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
      nullptr);
  if (device_ == INVALID_HANDLE_VALUE)
    throw_windows("open user HTTP/3 WFP gate device");
  (void)snapshot();
}

gate_controller::~gate_controller() {
  if (device_ != INVALID_HANDLE_VALUE)
    (void)::CloseHandle(device_);
}

contract::gate_telemetry gate_controller::snapshot() const {
  contract::gate_telemetry result{};
  DWORD bytes = 0;
  if (!::DeviceIoControl(
          device_, contract::query_telemetry_ioctl,
          nullptr, 0, &result, sizeof(result), &bytes, nullptr))
    throw_windows("query user HTTP/3 WFP gate telemetry");
  if (bytes != sizeof(result) ||
      result.version != contract::telemetry_version ||
      result.size != sizeof(result))
    throw std::runtime_error("user HTTP/3 WFP gate telemetry ABI mismatch");
  return result;
}

gate_policy gate_controller::install(std::uint16_t remote_port) {
  return install_policy(remote_port, false);
}

gate_policy gate_controller::install_unavailable(
    std::uint16_t remote_port) {
  return install_policy(remote_port, true);
}

gate_policy gate_controller::install_policy(
    std::uint16_t remote_port, bool unavailable) {
  if (remote_port == 0)
    throw std::invalid_argument("HTTP/3 WFP gate port must be nonzero");
  const auto before = snapshot();
  auto session = ntl::wfp::policy_session::ephemeral(
      unavailable
          ? L"crtsys user HTTP/3 unavailable-callout proof"
          : L"crtsys user HTTP/3 app-scoped gate");
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        unavailable
            ? ntl::wfp::provider_spec{
                  contract::unavailable_provider_key,
                  L"crtsys user HTTP/3 unavailable provider",
                  L"Negative proof with deliberately unregistered callouts"}
            : ntl::wfp::provider_spec{
                  contract::provider_key,
                  L"crtsys user HTTP/3 gate provider",
                  L"App-scoped dual-stack UDP policy before user MsQuic"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        unavailable
            ? ntl::wfp::sublayer_spec{
                  contract::unavailable_sublayer_key,
                  L"crtsys user HTTP/3 unavailable sublayer",
                  L"Fail closed when the exact callout key is absent",
                  0x7651}
            : ntl::wfp::sublayer_spec{
                  contract::sublayer_key,
                  L"crtsys user HTTP/3 gate sublayer",
                  L"Typed ALE policy before user-mode QUIC termination",
                  0x7650});

    if (unavailable) {
      const auto callout_v4 =
          transaction.add_callout<contract::layer_v4>(
              provider,
              {contract::unavailable_callout_key_v4,
               L"Deliberately unavailable IPv4 HTTP/3 callout", L""});
      const auto callout_v6 =
          transaction.add_callout<contract::layer_v6>(
              provider,
              {contract::unavailable_callout_key_v6,
               L"Deliberately unavailable IPv6 HTTP/3 callout", L""});
      ntl::wfp::filter_builder<contract::layer_v4> filter_v4(
          contract::unavailable_filter_key_v4,
          L"Block IPv4 HTTP/3 when its callout is unavailable",
          ntl::wfp::callout_unavailable::block);
      filter_v4.application_equal(application_)
          .protocol_equal(IPPROTO_UDP)
          .remote_port_equal(remote_port);
      transaction.add_filter(sublayer, callout_v4, filter_v4);
      ntl::wfp::filter_builder<contract::layer_v6> filter_v6(
          contract::unavailable_filter_key_v6,
          L"Block IPv6 HTTP/3 when its callout is unavailable",
          ntl::wfp::callout_unavailable::block);
      filter_v6.application_equal(application_)
          .protocol_equal(IPPROTO_UDP)
          .remote_port_equal(remote_port);
      transaction.add_filter(sublayer, callout_v6, filter_v6);
      return;
    }

    const auto callout_v4 = transaction.add_callout<contract::layer_v4>(
        provider,
        {contract::callout_key_v4,
         L"Permit and capture selected IPv4 user HTTP/3", L""});
    const auto callout_v6 = transaction.add_callout<contract::layer_v6>(
        provider,
        {contract::callout_key_v6,
         L"Permit and capture selected IPv6 user HTTP/3", L""});
    ntl::wfp::filter_builder<contract::layer_v4> filter_v4(
        contract::filter_key_v4,
        L"Gate the current application's IPv4 HTTP/3 endpoint",
        ntl::wfp::callout_unavailable::block);
    filter_v4.application_equal(application_)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(remote_port);
    transaction.add_filter(sublayer, callout_v4, filter_v4);
    ntl::wfp::filter_builder<contract::layer_v6> filter_v6(
        contract::filter_key_v6,
        L"Gate the current application's IPv6 HTTP/3 endpoint",
        ntl::wfp::callout_unavailable::block);
    filter_v6.application_equal(application_)
        .protocol_equal(IPPROTO_UDP)
        .remote_port_equal(remote_port);
    transaction.add_filter(sublayer, callout_v6, filter_v6);
  });
  return gate_policy(std::move(session), before, remote_port, unavailable);
}

void gate_controller::verify_gate(
    const gate_policy &policy, int family) {
  if (policy.unavailable_)
    throw std::logic_error("normal WFP proof received unavailable policy");
  const auto after = snapshot();
  const auto &before_layer = layer_for(policy.before_, family);
  const auto &after_layer = layer_for(after, family);
  const std::uint64_t hit_delta =
      after_layer.classify_hits - before_layer.classify_hits;
  const std::uint64_t permit_delta =
      after_layer.permit_decisions - before_layer.permit_decisions;
  const std::uint64_t write_delta =
      after_layer.action_write_available -
      before_layer.action_write_available;
  if (hit_delta == 0 || permit_delta != hit_delta ||
      write_delta != hit_delta ||
      after_layer.invalid_protocol != before_layer.invalid_protocol ||
      after_layer.action_write_missing !=
          before_layer.action_write_missing ||
      after_layer.last_process_id != process_id_ ||
      after_layer.last_application_id_hash != application_hash_ ||
      after_layer.last_application_id_size != application_.bytes().size() ||
      after_layer.last_remote_port != policy.port_ ||
      after_layer.last_protocol != IPPROTO_UDP ||
      after_layer.address_family != family ||
      after_layer.last_filter_id == 0 ||
      !captured_loopback(after_layer, family))
    throw std::runtime_error(
        "user HTTP/3 WFP gate tuple/counter evidence is incomplete");

  if (family == AF_INET) {
    ipv4_delta_ += hit_delta;
    original_v4_port_ = after_layer.last_remote_port;
    gated_families_ |= 1u;
  } else {
    ipv6_delta_ += hit_delta;
    original_v6_port_ = after_layer.last_remote_port;
    gated_families_ |= 2u;
  }
}

void gate_controller::verify_unavailable(
    const gate_policy &policy, int family,
    std::uint64_t origin_hits) {
  if (!policy.unavailable_ || origin_hits != 0 ||
      !counters_equal(policy.before_, snapshot()))
    throw std::runtime_error(
        "unavailable HTTP/3 callout did not fail closed independently");
  unavailable_families_ |= family == AF_INET ? 1u : 2u;
}

void gate_controller::record_webtransport_rejection(int family) {
  if (family != AF_INET && family != AF_INET6)
    throw std::invalid_argument(
        "unsupported WebTransport rejection address family");
  webtransport_rejected_families_ |= family == AF_INET ? 1u : 2u;
}

void gate_controller::verify_direct_after_removal(
    int family, const contract::gate_telemetry &before,
    const contract::gate_telemetry &after) {
  if (!counters_equal(before, after))
    throw std::runtime_error(
        "removed HTTP/3 WFP policy still changed gate counters");
  direct_families_ |= family == AF_INET ? 1u : 2u;
}

gate_evidence gate_controller::evidence() const noexcept {
  return {
      .ipv4_delta = ipv4_delta_,
      .ipv6_delta = ipv6_delta_,
      .application_hash = application_hash_,
      .process_id = process_id_,
      .original_v4_port = original_v4_port_,
      .original_v6_port = original_v6_port_,
      .gated_families = gated_families_,
      .direct_families = direct_families_,
      .unavailable_families = unavailable_families_,
      .webtransport_rejected_families =
          webtransport_rejected_families_};
}

} // namespace crtsys::wfp_user_http3
