#include <ntddk.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/net/framing>
#include <ntl/net/inspection/core>
#include <ntl/wfp/all>

#include "tcp_content_filter_contract.hpp"

namespace {

namespace contract = wfp_kernel_tcp_content_filter;
using query_stats = ntl::ioctl_from_contract<contract::query_stats_contract>;

struct flow_context {};

struct filter_state {
  contract::filter_stats snapshot() const noexcept {
    return {
        inspected.load(std::memory_order_relaxed),
        permitted.load(std::memory_order_relaxed),
        blocked.load(std::memory_order_relaxed),
        malformed.load(std::memory_order_relaxed),
        failed.load(std::memory_order_relaxed),
    };
  }

  std::optional<ntl::wfp::flow_target<contract::stream_layer_v4, flow_context>>
      target_v4;
  std::optional<ntl::wfp::flow_target<contract::stream_layer_v6, flow_context>>
      target_v6;
  std::atomic<std::uint64_t> inspected{0};
  std::atomic<std::uint64_t> permitted{0};
  std::atomic<std::uint64_t> blocked{0};
  std::atomic<std::uint64_t> malformed{0};
  std::atomic<std::uint64_t> failed{0};
};

template <class StreamLayer>
ntl::wfp::stream_result inspect_stream(
    filter_state &state,
    const ntl::wfp::stream_event<StreamLayer, flow_context> &event) noexcept {
  if (!event.context()) {
    state.failed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }

  const auto data = event.data();
  if ((data.flags() & FWPS_STREAM_FLAG_RECEIVE) == 0)
    return ntl::wfp::stream_result::permit(data.size());

  if (event.missed_bytes() != 0 || event.buffer_limit_reached()) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }
  if (data.empty()) {
    if (event.no_more_data())
      return ntl::wfp::stream_result::permit(0);
    return ntl::wfp::stream_result::need_more(
        static_cast<std::uint32_t>(contract::length_prefix_size));
  }

  const ntl::net::framing::u32_be_length_prefix framer{
      contract::maximum_record_size};
  const auto probe = ntl::net::framing::validate(
      framer.probe(data.bytes()), data.size(),
      ntl::net::framing::frame_limits{contract::maximum_frame_size});
  if (probe.state() == ntl::net::framing::probe_state::malformed ||
      (probe.state() == ntl::net::framing::probe_state::need_more &&
       event.no_more_data())) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }
  if (probe.state() == ntl::net::framing::probe_state::need_more) {
    const std::size_t additional =
        probe.required_total() > data.size()
            ? probe.required_total() - data.size()
            : 1;
    return ntl::wfp::stream_result::need_more(
        static_cast<std::uint32_t>(additional));
  }

  const auto record_bytes = data.bytes().subview(
      contract::length_prefix_size,
      probe.frame_size() - contract::length_prefix_size);
  if (!record_bytes) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }

  state.inspected.fetch_add(1, std::memory_order_relaxed);
  const auto verdict = crtsys::examples::wfp::content_filter::decide(
      ntl::net::inspection::content_view(*record_bytes),
      contract::maximum_record_body_size);
  if (verdict == ntl::net::inspection::verdict::drop_flow) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }
  if (verdict == ntl::net::inspection::verdict::block) {
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::stream_result::drop_connection();
  }

  state.permitted.fetch_add(1, std::memory_order_relaxed);
  return ntl::wfp::stream_result::permit(probe.frame_size());
}

template <class FlowLayer, class StreamLayer>
ntl::wfp::arbitration_decision begin_flow(
    filter_state &state,
    const ntl::wfp::classify_event<FlowLayer> &event) noexcept {
  auto &target = [&]() -> auto & {
    if constexpr (std::is_same_v<StreamLayer, contract::stream_layer_v4>)
      return state.target_v4;
    else
      return state.target_v6;
  }();
  const auto flow = event.metadata().flow_handle();
  const auto protocol = event.value(FlowLayer::field::protocol).uint8();
  const auto direction = event.value(FlowLayer::field::direction).uint32();
  if (!flow || !protocol || !direction) {
    state.failed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::arbitration_decision::block;
  }
  if (*protocol != IPPROTO_TCP || *direction != FWP_DIRECTION_INBOUND)
    return ntl::wfp::arbitration_decision::continue_classification;

  if (!target) {
    state.failed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::arbitration_decision::block;
  }

  try {
    auto context = std::unique_ptr<flow_context>(new flow_context{});
    const ntl::status associated =
        target->associate(*flow, std::move(context));
    if (associated.is_ok())
      return ntl::wfp::arbitration_decision::continue_classification;
  } catch (...) {
  }
  state.failed.fetch_add(1, std::memory_order_relaxed);
  state.blocked.fetch_add(1, std::memory_order_relaxed);
  return ntl::wfp::arbitration_decision::block;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<filter_state>();

  auto options = ntl::device_options()
                     .name(contract::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          contract::device_class_guid);
  auto device_result =
      ntl::try_create_device_endpoint<void>(driver, options);
  if (!device_result)
    return device_result.status();
  auto endpoint = std::move(*device_result);
  const ntl::status query_route =
      endpoint.on_ioctl<contract::query_stats_contract>(
          [state](contract::filter_stats &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok())
    return query_route;

  ntl::wfp::callout_driver<> callouts(driver);
  auto stream_v4 = callouts.add_stream<flow_context>(
      contract::stream_callout_key_v4, state,
      [](filter_state &owned_state,
         const ntl::wfp::stream_event<contract::stream_layer_v4,
                                      flow_context> &event) noexcept {
        return inspect_stream(owned_state, event);
      });
  if (!stream_v4)
    return stream_v4.status();
  state->target_v4 = *stream_v4;

  auto stream_v6 = callouts.add_stream<flow_context>(
      contract::stream_callout_key_v6, state,
      [](filter_state &owned_state,
         const ntl::wfp::stream_event<contract::stream_layer_v6,
                                      flow_context> &event) noexcept {
        return inspect_stream(owned_state, event);
      });
  if (!stream_v6)
    return stream_v6.status();
  state->target_v6 = *stream_v6;

  const ntl::status flow_v4 = callouts.add_arbitrating(
      contract::flow_callout_key_v4, state,
      [](filter_state &owned_state,
         const ntl::wfp::classify_event<contract::flow_layer_v4> &event)
          noexcept {
        return begin_flow<contract::flow_layer_v4,
                          contract::stream_layer_v4>(owned_state, event);
      });
  if (!flow_v4.is_ok())
    return flow_v4;

  const ntl::status flow_v6 = callouts.add_arbitrating(
      contract::flow_callout_key_v6, state,
      [](filter_state &owned_state,
         const ntl::wfp::classify_event<contract::flow_layer_v6> &event)
          noexcept {
        return begin_flow<contract::flow_layer_v6,
                          contract::stream_layer_v6>(owned_state, event);
      });
  if (!flow_v6.is_ok())
    return flow_v6;

  driver.on_unload([state, endpoint, callouts]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
  });
  return ntl::status::ok();
}
