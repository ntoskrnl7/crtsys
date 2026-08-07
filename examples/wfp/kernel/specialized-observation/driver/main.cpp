#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "specialized_observation_contract.hpp"

namespace {

using query_stats = ntl::ioctl_from_contract<
    wfp_specialized_observation::query_stats_contract>;

class observation_state {
public:
  template <class Layer>
  void observe(
      wfp_specialized_observation::counter index) noexcept {
    const auto offset = static_cast<std::size_t>(index);
    indications_[offset].fetch_add(1, std::memory_order_relaxed);
    telemetry_.record_classify(Layer::runtime_id);
    telemetry_.record_permit(Layer::runtime_id);
  }

  void registered(wfp_specialized_observation::counter index) noexcept {
    registered_mask_.fetch_or(
        1u << static_cast<std::size_t>(index),
        std::memory_order_relaxed);
  }

  wfp_specialized_observation::observation_stats snapshot() const noexcept {
    wfp_specialized_observation::observation_stats result{};
    result.version = 1;
    result.registered_mask =
        registered_mask_.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index != result.indications.size(); ++index)
      result.indications[index] =
          indications_[index].load(std::memory_order_relaxed);
    return result;
  }

private:
  std::array<std::atomic<std::uint64_t>,
             wfp_specialized_observation::counter_count>
      indications_{};
  std::atomic<std::uint32_t> registered_mask_{0};
  ntl::wfp::operational_telemetry telemetry_;
};

template <class Layer, wfp_specialized_observation::counter Index>
ntl::status register_callout(
    ntl::wfp::callout_driver<16> &callouts,
    ntl::wfp::inspection_callout_key<Layer> key,
    const std::shared_ptr<observation_state> &state) noexcept {
  const auto result = callouts.add_inspection(
      key, state,
      [](observation_state &owned_state,
         const ntl::wfp::classify_event<Layer> &) noexcept {
        owned_state.observe<Layer>(Index);
      });
  if (result.is_ok())
    state->registered(Index);
  return result;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<observation_state>();
  auto options =
      ntl::device_options()
          .name(wfp_specialized_observation::device_name)
          .type(FILE_DEVICE_UNKNOWN)
          .exclusive(false)
          .security_descriptor(
              L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
              wfp_specialized_observation::device_class_guid);
  auto endpoint_result =
      ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route =
      endpoint.on_ioctl<
          wfp_specialized_observation::query_stats_contract>(
          [state](wfp_specialized_observation::observation_stats
                      &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok())
    return query_route;

  ntl::wfp::callout_driver<16> callouts(driver);

#define NTL_REGISTER_SPECIALIZED(name, layer, index)                            \
  do {                                                                           \
    const auto status = register_callout<                                        \
        wfp_specialized_observation::layer,                                      \
        wfp_specialized_observation::counter::index>(                            \
        callouts,                                                                \
        wfp_specialized_observation::name##_callout_key, state);                 \
    if (!status.is_ok()) {                                                       \
      return status;                                                             \
    }                                                                            \
  } while (false)

  NTL_REGISTER_SPECIALIZED(endpoint_v4, endpoint_v4, endpoint_v4);
  NTL_REGISTER_SPECIALIZED(endpoint_v6, endpoint_v6, endpoint_v6);
  NTL_REGISTER_SPECIALIZED(mac_in, mac_in, mac_in);
  NTL_REGISTER_SPECIALIZED(mac_out, mac_out, mac_out);
  NTL_REGISTER_SPECIALIZED(vswitch_in, vswitch_in, vswitch_in);
  NTL_REGISTER_SPECIALIZED(vswitch_out, vswitch_out, vswitch_out);

#undef NTL_REGISTER_SPECIALIZED

  driver.on_unload([state, endpoint, callouts] {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const auto result = callouts.close();
    NT_ASSERT(result.is_ok());
    NT_ASSERT(
        state->snapshot().registered_mask ==
        wfp_specialized_observation::all_layers_mask);
  });
  return ntl::status::ok();
}
