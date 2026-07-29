#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/ndis/all>

#include "lwf_monitor_contract.hpp"

namespace {

struct monitor_state {
  std::atomic<std::uint64_t> modules_attached{0};
  std::atomic<std::uint64_t> modules_detached{0};
  std::atomic<std::uint64_t> restarts{0};
  std::atomic<std::uint64_t> pauses{0};
  std::atomic<std::uint64_t> send_lists{0};
  std::atomic<std::uint64_t> send_completions{0};
  std::atomic<std::uint64_t> receive_lists{0};
  std::atomic<std::uint64_t> send_bytes{0};
  std::atomic<std::uint64_t> receive_bytes{0};
  std::atomic<std::uint64_t> checksum_metadata{0};
  std::atomic<std::uint64_t> large_send_metadata{0};
  std::atomic<std::uint64_t> receive_coalescing_metadata{0};
  std::atomic<std::uint64_t> vlan_metadata{0};
  std::atomic<std::uint64_t> receive_hash_metadata{0};

  ndis_lwf_monitor::monitor_stats snapshot() const noexcept {
    return {
        modules_attached.load(std::memory_order_relaxed),
        modules_detached.load(std::memory_order_relaxed),
        restarts.load(std::memory_order_relaxed),
        pauses.load(std::memory_order_relaxed),
        send_lists.load(std::memory_order_relaxed),
        send_completions.load(std::memory_order_relaxed),
        receive_lists.load(std::memory_order_relaxed),
        send_bytes.load(std::memory_order_relaxed),
        receive_bytes.load(std::memory_order_relaxed),
        checksum_metadata.load(std::memory_order_relaxed),
        large_send_metadata.load(std::memory_order_relaxed),
        receive_coalescing_metadata.load(std::memory_order_relaxed),
        vlan_metadata.load(std::memory_order_relaxed),
        receive_hash_metadata.load(std::memory_order_relaxed),
    };
  }
};

monitor_state *g_state = nullptr;

class monitor_module {
public:
  explicit monitor_module(ntl::ndis::attach_event event) noexcept
      : state_(g_state), interface_index_(event.interface_index()) {
    const auto mac = event.current_mac_address();
    mac_address_length_ = mac.size();
    if (state_)
      state_->modules_attached.fetch_add(1, std::memory_order_relaxed);
  }

  ~monitor_module() noexcept {
    if (state_)
      state_->modules_detached.fetch_add(1, std::memory_order_relaxed);
  }

  ntl::status on_restart(ntl::ndis::restart_event) noexcept {
    if (state_)
      state_->restarts.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  ntl::status on_pause(ntl::ndis::pause_event) noexcept {
    if (state_)
      state_->pauses.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  void on_send(ntl::ndis::send_event event) noexcept {
    if (!state_)
      return;
    state_->send_lists.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    state_->send_bytes.fetch_add(
        event.packets().byte_count(), std::memory_order_relaxed);
    observe_metadata(event.packets());
  }

  void on_send_complete(
      ntl::ndis::send_complete_event event) noexcept {
    if (state_)
      state_->send_completions.fetch_add(
          event.packets().list_count(), std::memory_order_relaxed);
  }

  void on_receive(ntl::ndis::receive_event event) noexcept {
    if (!state_)
      return;
    state_->receive_lists.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    state_->receive_bytes.fetch_add(
        event.packets().byte_count(), std::memory_order_relaxed);
    observe_metadata(event.packets());
  }

private:
  void observe_metadata(
      ntl::ndis::net_buffer_list_chain_view chain) noexcept {
    (void)chain.for_each([this](
        ntl::ndis::net_buffer_list_view packet) noexcept {
      if (packet.checksum().present())
        state_->checksum_metadata.fetch_add(
            1, std::memory_order_relaxed);
      if (packet.large_send().present())
        state_->large_send_metadata.fetch_add(
            1, std::memory_order_relaxed);
      if (packet.receive_segment_coalescing().present())
        state_->receive_coalescing_metadata.fetch_add(
            1, std::memory_order_relaxed);
      if (packet.vlan().present())
        state_->vlan_metadata.fetch_add(
            1, std::memory_order_relaxed);
      if (packet.receive_hash().present())
        state_->receive_hash_metadata.fetch_add(
            1, std::memory_order_relaxed);
      return true;
    });
  }

  monitor_state *state_ = nullptr;
  NET_IFINDEX interface_index_ = 0;
  std::size_t mac_address_length_ = 0;
};

ntl::status validate_reassembly_contract() noexcept {
  ntl::ndis::bounded_tcp_reassembler<4> reassembler(
      0xfffffffcu,
      {.maximum_buffered_bytes = 64,
       .maximum_forward_window = 64});
  constexpr std::array<std::byte, 3> tail{
      std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};
  constexpr std::array<std::byte, 4> head{
      std::byte{'0'}, std::byte{'1'}, std::byte{'2'},
      std::byte{'3'}};
  std::array<std::byte, 7> output{};
  std::size_t offset = 0;
  auto sink = [&output, &offset](
                  ntl::net::scatter_view bytes) noexcept -> ntl::status {
    if (bytes.size() > output.size() - offset)
      return STATUS_BUFFER_OVERFLOW;
    const ntl::status copied = bytes.copy_to(
        std::span<std::byte>(output).subspan(offset, bytes.size()));
    if (copied.is_ok())
      offset += bytes.size();
    return copied;
  };

  auto buffered = reassembler.push(
      {0u, ntl::net::scatter_view::from_contiguous(tail), true},
      sink, ntl::pool_tag("rMwN"));
  if (!buffered ||
      buffered->state != ntl::ndis::tcp_reassembly_state::buffered)
    return STATUS_INTERNAL_ERROR;

  auto finished = reassembler.push(
      {0xfffffffcu, ntl::net::scatter_view::from_contiguous(head), false},
      sink, ntl::pool_tag("rMwN"));
  constexpr std::array<std::byte, 7> expected{
      std::byte{'0'}, std::byte{'1'}, std::byte{'2'},
      std::byte{'3'}, std::byte{'d'}, std::byte{'e'},
      std::byte{'f'}};
  if (!finished ||
      finished->state != ntl::ndis::tcp_reassembly_state::finished ||
      offset != expected.size() ||
      std::memcmp(output.data(), expected.data(), expected.size()) != 0)
    return STATUS_INTERNAL_ERROR;

  ntl::ndis::bounded_tcp_reassembler<2> overlap(
      10, {.maximum_buffered_bytes = 8,
           .maximum_forward_window = 8});
  constexpr std::array<std::byte, 2> bytes{};
  auto first = overlap.push(
      {12, ntl::net::scatter_view::from_contiguous(bytes), false},
      sink, ntl::pool_tag("oMwN"));
  auto second = overlap.push(
      {13, ntl::net::scatter_view::from_contiguous(bytes), false},
      sink, ntl::pool_tag("oMwN"));
  if (!first || second ||
      static_cast<NTSTATUS>(second.status()) != STATUS_DATA_ERROR)
    return STATUS_INTERNAL_ERROR;

  return ntl::status::ok();
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status reassembly = validate_reassembly_contract();
  if (!reassembly.is_ok())
    return reassembly;

  auto state = std::make_shared<monitor_state>();
  auto options =
      ntl::device_options()
          .name(ndis_lwf_monitor::device_name)
          .type(FILE_DEVICE_UNKNOWN)
          .exclusive(false)
          .security_descriptor(
              L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
              ndis_lwf_monitor::device_class_guid);
  auto endpoint_result =
      ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();
  auto endpoint = std::move(*endpoint_result);

  ntl::status endpoint_status = endpoint.on_create(
      [](ntl::irp &request) noexcept { request.succeed(); });
  if (!endpoint_status.is_ok())
    return endpoint_status;
  endpoint_status = endpoint.on_close(
      [](ntl::irp &request) noexcept { request.succeed(); });
  if (!endpoint_status.is_ok())
    return endpoint_status;
  endpoint_status =
      endpoint.on_ioctl<ndis_lwf_monitor::query_stats_contract>(
          [state](ndis_lwf_monitor::monitor_stats &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          });
  if (!endpoint_status.is_ok())
    return endpoint_status;

  g_state = state.get();
  auto registration =
      ntl::ndis::lightweight_filter<monitor_module>::try_create(
          driver.native_handle(),
          {ndis_lwf_monitor::filter_friendly_name,
           ndis_lwf_monitor::filter_unique_name,
           ndis_lwf_monitor::service_name, 1, 0});
  if (!registration) {
    g_state = nullptr;
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    return registration.status();
  }

  driver.on_unload(
      [state, endpoint,
       registration = *registration]() mutable noexcept {
        const ntl::status closed = endpoint.close();
        NT_ASSERT(closed.is_ok());
        registration->reset();
        g_state = nullptr;
      });
  return ntl::status::ok();
}
