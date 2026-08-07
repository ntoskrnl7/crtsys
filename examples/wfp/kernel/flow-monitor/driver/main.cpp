#include <ntddk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/wfp/all>

#include "flow_monitor_contract.hpp"

namespace {

using query_stats =
    ntl::ioctl_from_contract<wfp_flow_monitor::query_stats_contract>;

struct monitor_state;

struct monitor_flow {
  explicit monitor_flow(monitor_state &value) noexcept : state(&value) {}
  ~monitor_flow() noexcept;
  monitor_state *state;
};

struct monitor_state {
  std::optional<
      ntl::wfp::flow_target<wfp_flow_monitor::stream_layer_v4, monitor_flow>>
      target_v4;
  std::optional<
      ntl::wfp::flow_target<wfp_flow_monitor::stream_layer_v6, monitor_flow>>
      target_v6;
  std::atomic<std::uint64_t> flows_started{0};
  std::atomic<std::uint64_t> flows_closed{0};
  std::atomic<std::uint64_t> stream_indications{0};
  std::atomic<std::uint64_t> stream_bytes{0};
  std::atomic<std::uint64_t> missed_bytes{0};

  wfp_flow_monitor::monitor_stats snapshot() const noexcept {
    return {
        flows_started.load(std::memory_order_relaxed),
        flows_closed.load(std::memory_order_relaxed),
        stream_indications.load(std::memory_order_relaxed),
        stream_bytes.load(std::memory_order_relaxed),
        missed_bytes.load(std::memory_order_relaxed),
    };
  }
};

monitor_flow::~monitor_flow() noexcept {
  state->flows_closed.fetch_add(1, std::memory_order_relaxed);
}

template <class StreamLayer>
void observe_stream(
    monitor_state &,
    const ntl::wfp::stream_event<StreamLayer, monitor_flow> &event) noexcept {
  monitor_flow *const flow = event.context();
  const auto data = event.data();
  if (flow) {
    flow->state->stream_indications.fetch_add(1, std::memory_order_relaxed);
    flow->state->stream_bytes.fetch_add(data.size(), std::memory_order_relaxed);
    flow->state->missed_bytes.fetch_add(event.missed_bytes(),
                                        std::memory_order_relaxed);
  }
}

template <class FlowLayer, class StreamLayer>
void begin_flow(monitor_state &state,
                const ntl::wfp::classify_event<FlowLayer> &event) noexcept {
  auto &target = [&]() -> auto & {
    if constexpr (std::is_same_v<StreamLayer,
                                 wfp_flow_monitor::stream_layer_v4>)
      return state.target_v4;
    else
      return state.target_v6;
  }();
  if (!target)
    return;

  const auto handle = event.metadata().flow_handle();
  const auto protocol = event.value(FlowLayer::field::protocol).uint8();
  const auto direction = event.value(FlowLayer::field::direction).uint32();
  if (!handle || !protocol || !direction || *protocol != IPPROTO_TCP ||
      *direction != FWP_DIRECTION_OUTBOUND)
    return;

  std::unique_ptr<monitor_flow> context(new (std::nothrow)
                                            monitor_flow(state));
  if (!context)
    return;

  const ntl::status associated = target->associate(*handle, std::move(context));
  if (associated.is_ok())
    state.flows_started.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<monitor_state>();

  auto options = ntl::device_options()
                     .name(wfp_flow_monitor::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          wfp_flow_monitor::device_class_guid);
  auto endpoint_result = ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route =
      endpoint.on_ioctl<wfp_flow_monitor::query_stats_contract>(
          [state](wfp_flow_monitor::monitor_stats &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok())
    return query_route;

  ntl::wfp::callout_driver<> callouts(driver);
  auto target_v4 = callouts.add_stream_inspection<monitor_flow>(
      wfp_flow_monitor::stream_callout_key_v4, state,
      [](monitor_state &owned_state,
         const ntl::wfp::stream_event<wfp_flow_monitor::stream_layer_v4,
                                      monitor_flow> &event) noexcept {
        observe_stream(owned_state, event);
      });
  if (!target_v4)
    return target_v4.status();
  state->target_v4 = *target_v4;

  auto target_v6 = callouts.add_stream_inspection<monitor_flow>(
      wfp_flow_monitor::stream_callout_key_v6, state,
      [](monitor_state &owned_state,
         const ntl::wfp::stream_event<wfp_flow_monitor::stream_layer_v6,
                                      monitor_flow> &event) noexcept {
        observe_stream(owned_state, event);
      });
  if (!target_v6)
    return target_v6.status();
  state->target_v6 = *target_v6;

  const ntl::status flow_status_v4 = callouts.add_inspection(
      wfp_flow_monitor::flow_callout_key_v4, state,
      [](monitor_state &owned_state,
         const ntl::wfp::classify_event<
             wfp_flow_monitor::flow_layer_v4> &event) noexcept {
        begin_flow<wfp_flow_monitor::flow_layer_v4,
                   wfp_flow_monitor::stream_layer_v4>(owned_state, event);
      });
  if (!flow_status_v4.is_ok())
    return flow_status_v4;

  const ntl::status flow_status_v6 = callouts.add_inspection(
      wfp_flow_monitor::flow_callout_key_v6, state,
      [](monitor_state &owned_state,
         const ntl::wfp::classify_event<
             wfp_flow_monitor::flow_layer_v6> &event) noexcept {
        begin_flow<wfp_flow_monitor::flow_layer_v6,
                   wfp_flow_monitor::stream_layer_v6>(owned_state, event);
      });
  if (!flow_status_v6.is_ok())
    return flow_status_v6;

  driver.on_unload([state, endpoint, callouts] {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
