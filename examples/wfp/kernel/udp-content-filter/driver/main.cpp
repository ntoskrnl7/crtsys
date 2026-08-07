#include <ntddk.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/net/buffer/scatter_view>
#include <ntl/net/inspection/core>
#include <ntl/wfp/all>

#include "udp_content_filter_contract.hpp"

namespace {

namespace contract = wfp_kernel_udp_content_filter;
using query_stats = ntl::ioctl_from_contract<contract::query_stats_contract>;

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
  std::atomic<std::uint64_t> inspected{0};
  std::atomic<std::uint64_t> permitted{0};
  std::atomic<std::uint64_t> blocked{0};
  std::atomic<std::uint64_t> malformed{0};
  std::atomic<std::uint64_t> failed{0};
};

template <class Layer>
ntl::wfp::terminating_decision inspect_datagram(
    filter_state &state,
    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto packet = event.packet();
  if (!packet) {
    state.failed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  const auto direction = event.value(Layer::field::direction).uint32();
  const auto protocol = event.value(Layer::field::protocol).uint8();
  if (!direction || !protocol) {
    state.failed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  if (*direction != FWP_DIRECTION_OUTBOUND || *protocol != IPPROTO_UDP)
    return ntl::wfp::terminating_decision::permit;

  NET_BUFFER_LIST *const list = packet.borrowed_native_handle();
  if (!list || NET_BUFFER_LIST_NEXT_NBL(list) ||
      !NET_BUFFER_LIST_FIRST_NB(list) ||
      NET_BUFFER_NEXT_NB(NET_BUFFER_LIST_FIRST_NB(list))) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  constexpr std::size_t udp_header_size = 8;
  const auto bytes = packet.bytes();
  ntl::net::borrowed_byte_cursor cursor(bytes);
  const auto source_port = cursor.read_be16();
  const auto destination_port = cursor.read_be16();
  const auto udp_length = cursor.read_be16();
  const auto checksum = cursor.read_be16();
  (void)source_port;
  (void)destination_port;
  (void)checksum;
  if (!source_port || !destination_port || !udp_length || !checksum ||
      *udp_length < udp_header_size || *udp_length != bytes.size() ||
      static_cast<std::size_t>(*udp_length - udp_header_size) >
          contract::maximum_record_size) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  const std::size_t payload_size = *udp_length - udp_header_size;
  const auto payload = bytes.subview(udp_header_size, payload_size);
  if (!payload) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  state.inspected.fetch_add(1, std::memory_order_relaxed);
  const auto verdict = crtsys::examples::wfp::content_filter::decide(
      ntl::net::inspection::content_view(*payload),
      contract::maximum_record_body_size);
  if (verdict == ntl::net::inspection::verdict::drop_flow) {
    state.malformed.fetch_add(1, std::memory_order_relaxed);
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  if (verdict == ntl::net::inspection::verdict::block) {
    state.blocked.fetch_add(1, std::memory_order_relaxed);
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  state.permitted.fetch_add(1, std::memory_order_relaxed);
  return ntl::wfp::terminating_decision::permit;
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
  auto endpoint_result =
      ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);
  const ntl::status query_route =
      endpoint.on_ioctl<contract::query_stats_contract>(
          [state](contract::filter_stats &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!query_route.is_ok())
    return query_route;

  ntl::wfp::callout_driver<> callouts(driver);
  const ntl::status v4 = callouts.add_terminating(
      contract::callout_key_v4, state,
      [](filter_state &owned_state,
         const ntl::wfp::classify_event<contract::layer_v4> &event) noexcept {
        return inspect_datagram(owned_state, event);
      });
  if (!v4.is_ok())
    return v4;

  const ntl::status v6 = callouts.add_terminating(
      contract::callout_key_v6, state,
      [](filter_state &owned_state,
         const ntl::wfp::classify_event<contract::layer_v6> &event) noexcept {
        return inspect_datagram(owned_state, event);
      });
  if (!v6.is_ok())
    return v6;

  driver.on_unload([endpoint, callouts]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
  });
  return ntl::status::ok();
}
