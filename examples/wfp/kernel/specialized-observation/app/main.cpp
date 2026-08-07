#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <ntl/wfp/all>

#include "runtime_controller.hpp"
#include "specialized_observation_contract.hpp"

namespace {

namespace runtime = crtsys::examples::wfp::runtime;

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      ::CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

template <class Layer>
void add_observer(ntl::wfp::policy_transaction &transaction,
                  ntl::wfp::provider_ref provider,
                  ntl::wfp::sublayer_ref sublayer,
                  ntl::wfp::inspection_callout_key<Layer> callout_key,
                  ntl::wfp::filter_key<Layer> filter_key,
                  const wchar_t *name) {
  const auto callout = transaction.add_callout<Layer>(
      provider, {callout_key, name, L"Specialized WFP runtime observation"});
  ntl::wfp::inspection_filter_builder<Layer> filter(
      filter_key, std::wstring(name) + L" filter");
  transaction.add_inspection_filter(sublayer, callout, filter);
}

template <class Layer>
void add_endpoint_observer(ntl::wfp::policy_transaction &transaction,
                           ntl::wfp::provider_ref provider,
                           ntl::wfp::sublayer_ref sublayer,
                           ntl::wfp::inspection_callout_key<Layer> callout_key,
                           ntl::wfp::filter_key<Layer> filter_key,
                           const ntl::wfp::application_id &application,
                           const wchar_t *name) {
  const auto callout = transaction.add_callout<Layer>(
      provider, {callout_key, name, L"Endpoint closure observation"});
  ntl::wfp::inspection_filter_builder<Layer> filter(
      filter_key, std::wstring(name) + L" filter");
  filter.application_equal(application).protocol_equal(IPPROTO_TCP);
  transaction.add_inspection_filter(sublayer, callout, filter);
}

void install_policy(ntl::wfp::policy_session &session,
                    const ntl::wfp::application_id &application) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_specialized_observation::provider_key,
         L"crtsys NTL WFP specialized observation provider",
         L"Runtime capability and Driver Verifier fixture"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_specialized_observation::sublayer_key,
         L"crtsys NTL WFP specialized observation sublayer",
         L"Inspection-only specialized-layer filters", 0x7060});

    add_endpoint_observer<wfp_specialized_observation::endpoint_v4>(
        transaction, provider, sublayer,
        wfp_specialized_observation::endpoint_v4_callout_key,
        wfp_specialized_observation::endpoint_v4_filter_key, application,
        L"Endpoint closure IPv4");
    add_endpoint_observer<wfp_specialized_observation::endpoint_v6>(
        transaction, provider, sublayer,
        wfp_specialized_observation::endpoint_v6_callout_key,
        wfp_specialized_observation::endpoint_v6_filter_key, application,
        L"Endpoint closure IPv6");

#define NTL_ADD_SPECIALIZED(name, layer, title)                                \
  add_observer<wfp_specialized_observation::layer>(                            \
      transaction, provider, sublayer,                                         \
      wfp_specialized_observation::name##_callout_key,                         \
      wfp_specialized_observation::name##_filter_key, title)
    NTL_ADD_SPECIALIZED(mac_in, mac_in, L"Inbound MAC frame");
    NTL_ADD_SPECIALIZED(mac_out, mac_out, L"Outbound MAC frame");
    NTL_ADD_SPECIALIZED(vswitch_in, vswitch_in, L"Ingress vSwitch frame");
    NTL_ADD_SPECIALIZED(vswitch_out, vswitch_out, L"Egress vSwitch frame");
#undef NTL_ADD_SPECIALIZED
  });
}

wfp_specialized_observation::observation_stats query_stats(HANDLE device) {
  wfp_specialized_observation::observation_stats result{};
  DWORD returned = 0;
  if (!::DeviceIoControl(
          device, wfp_specialized_observation::query_stats_ioctl, nullptr, 0,
          &result, sizeof(result), &returned, nullptr) ||
      returned != sizeof(result))
    throw std::system_error(::GetLastError(), std::system_category(),
                            "DeviceIoControl(specialized observation)");
  if (result.version != 1)
    throw std::runtime_error("specialized-observation ABI version mismatch");
  return result;
}

template <class Layer>
void require_layer(ntl::wfp::policy_session &session, const char *name) {
  const auto layer = session.inspect_layer<Layer>();
  if (!layer || layer->layer_id == 0)
    throw std::runtime_error(std::string("required WFP layer unavailable: ") +
                             name);
}

std::string stats_text(
    const wfp_specialized_observation::observation_stats &before,
    const wfp_specialized_observation::observation_stats &after) {
  std::ostringstream output;
  output << "before.registered_mask=" << before.registered_mask << '\n'
         << "after.registered_mask=" << after.registered_mask << '\n';
  for (std::size_t index = 0; index != before.indications.size(); ++index) {
    output << "before.indication" << index << '=' << before.indications[index]
           << '\n'
           << "after.indication" << index << '=' << after.indications[index]
           << '\n';
  }
  return output.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    runtime::arguments arguments(argc, argv);
    const auto application_path =
        std::filesystem::canonical(arguments.required(L"--application-path"));
    const auto lifecycle = runtime::parse_lifecycle(arguments);
    arguments.finish();
    if (!std::filesystem::is_regular_file(application_path))
      throw std::invalid_argument("observed application is not a regular file");
    const auto application =
        ntl::wfp::application_id::from_path(application_path.wstring());

    handle_owner device(::CreateFileW(
        wfp_specialized_observation::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(::GetLastError(), std::system_category(),
                              "CreateFile(specialized observation)");

    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys NTL WFP specialized observation controller");
    require_layer<wfp_specialized_observation::endpoint_v4>(
        policy, "ALE_ENDPOINT_CLOSURE_V4");
    require_layer<wfp_specialized_observation::endpoint_v6>(
        policy, "ALE_ENDPOINT_CLOSURE_V6");
    require_layer<wfp_specialized_observation::mac_in>(
        policy, "INBOUND_MAC_FRAME_ETHERNET");
    require_layer<wfp_specialized_observation::mac_out>(
        policy, "OUTBOUND_MAC_FRAME_ETHERNET");
    require_layer<wfp_specialized_observation::vswitch_in>(
        policy, "INGRESS_VSWITCH_ETHERNET");
    require_layer<wfp_specialized_observation::vswitch_out>(
        policy, "EGRESS_VSWITCH_ETHERNET");
    require_layer<wfp_specialized_observation::fast_in>(
        policy, "INBOUND_TRANSPORT_FAST");
    require_layer<wfp_specialized_observation::fast_out>(
        policy, "OUTBOUND_TRANSPORT_FAST");
    require_layer<wfp_specialized_observation::ipsec_v4>(policy, "IPSEC_V4");
    require_layer<wfp_specialized_observation::ipsec_v6>(policy, "IPSEC_V6");

    const auto before = query_stats(device.get());
    auto current = before;
    install_policy(policy, application);
    runtime::signal_ready(lifecycle);
    runtime::wait_for_stop(lifecycle, [&] { current = query_stats(device.get()); });
    current = query_stats(device.get());
    runtime::write_file(lifecycle.stats_file, stats_text(before, current));
    std::wcout << L"Specialized-observation controller stopped; telemetry "
                  L"captured and ephemeral policy removed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Specialized-observation controller failed: " << error.what()
              << '\n';
    return 1;
  }
}
