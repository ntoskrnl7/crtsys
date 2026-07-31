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
  ntl::wfp::decision observe(
      wfp_specialized_observation::counter index) noexcept {
    const auto offset = static_cast<std::size_t>(index);
    indications_[offset].fetch_add(1, std::memory_order_relaxed);
    telemetry_.record_classify(Layer::runtime_id);
    telemetry_.record_permit(Layer::runtime_id);
    return ntl::wfp::decision::continue_classification;
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

observation_state *g_state = nullptr;

template <class Layer, wfp_specialized_observation::counter Index>
ntl::wfp::decision observe(
    const ntl::wfp::classify_event<Layer> &) noexcept {
  auto *const state = g_state;
  return state ? state->observe<Layer>(Index)
               : ntl::wfp::decision::continue_classification;
}

template <class Layer, wfp_specialized_observation::counter Index>
ntl::status register_callout(
    ntl::wfp::callout_driver<16> &callouts,
    ntl::wfp::callout_key<Layer> key,
    observation_state &state) noexcept {
  const auto result = callouts.add<observe<Layer, Index>>(key);
  if (result.is_ok())
    state.registered(Index);
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
  auto endpoint = std::make_shared<ntl::device_endpoint<void>>(
      std::move(*endpoint_result));
  auto device = endpoint->device();
  if (!device)
    return STATUS_INVALID_DEVICE_STATE;

  device->on_create([](ntl::irp &request) { request.succeed(); });
  device->on_close([](ntl::irp &request) { request.succeed(); });
  device->on_device_control(
      [state](const ntl::device_control::code &code,
              const ntl::device_control::in_buffer &,
              ntl::device_control::out_buffer &out) {
        if (!ntl::is_ioctl<query_stats>(code)) {
          out.clear();
          throw ntl::exception(
              STATUS_INVALID_DEVICE_REQUEST,
              "unknown specialized-observation IOCTL");
        }
        if (!ntl::ioctl_write_output<query_stats>(
                out, state->snapshot()))
          throw ntl::exception(
              STATUS_BUFFER_TOO_SMALL,
              "specialized-observation output is too small");
      });

  auto callouts =
      std::make_shared<ntl::wfp::callout_driver<16>>(driver);
  g_state = state.get();

#define NTL_REGISTER_SPECIALIZED(name, layer, index)                            \
  do {                                                                           \
    const auto status = register_callout<                                        \
        wfp_specialized_observation::layer,                                      \
        wfp_specialized_observation::counter::index>(                            \
        *callouts,                                                               \
        wfp_specialized_observation::name##_callout_key, *state);                \
    if (!status.is_ok()) {                                                       \
      g_state = nullptr;                                                         \
      (void)callouts->reset();                                                   \
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
    endpoint->link().reset();
    const auto result = callouts->reset();
    NT_ASSERT(result.is_ok());
    g_state = nullptr;
    NT_ASSERT(
        state->snapshot().registered_mask ==
        wfp_specialized_observation::all_layers_mask);
    endpoint->reset();
  });
  return ntl::status::ok();
}
