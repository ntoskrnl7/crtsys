#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

#include <ntl/driver>
#include <ntl/ndis/all>

namespace {

class monitor_module {
public:
  explicit monitor_module(ntl::ndis::attach_event event) noexcept
      : interface_index_(event.interface_index()),
        mac_address_length_(event.current_mac_address().size()) {}

  ntl::status
  on_restart(ntl::ndis::restart_event) noexcept {
    running_.store(true, std::memory_order_release);
    return ntl::status::ok();
  }

  ntl::status on_pause(ntl::ndis::pause_event) noexcept {
    running_.store(false, std::memory_order_release);
    return ntl::status::ok();
  }

  void on_send(ntl::ndis::send_event event) noexcept {
    sends_.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    (void)event.packets().for_each(
        [](ntl::ndis::net_buffer_list_view packet) noexcept {
          (void)packet.byte_count();
          (void)packet.checksum();
          (void)packet.large_send();
          (void)packet.vlan();
          (void)packet.receive_hash();
          return true;
        });
  }

  void on_send_complete(
      ntl::ndis::send_complete_event event) noexcept {
    completions_.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
  }

  void on_receive(ntl::ndis::receive_event event) noexcept {
    receives_.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    (void)event.packets().for_each(
        [](ntl::ndis::net_buffer_list_view packet) noexcept {
          (void)packet.receive_segment_coalescing();
          return true;
        });
  }

private:
  NET_IFINDEX interface_index_ = 0;
  std::size_t mac_address_length_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> sends_{0};
  std::atomic<std::uint64_t> completions_{0};
  std::atomic<std::uint64_t> receives_{0};
};

static_assert(!std::is_copy_constructible_v<
              ntl::ndis::cloned_net_buffer_list>);
static_assert(std::is_trivially_copyable_v<
              ntl::ndis::net_buffer_list_chain_view>);
static_assert(std::is_constructible_v<
              monitor_module, ntl::ndis::attach_event>);

ntl::status exercise_reassembly_contract() noexcept {
  ntl::ndis::bounded_tcp_reassembler<4> reassembler(
      100, {.maximum_buffered_bytes = 64,
            .maximum_forward_window = 64});
  constexpr std::array<std::byte, 3> second{
      std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};
  constexpr std::array<std::byte, 3> first{
      std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
  std::size_t delivered = 0;
  auto sink = [&delivered](ntl::net::scatter_view bytes) noexcept {
    delivered += bytes.size();
    return ntl::status::ok();
  };
  auto buffered = reassembler.push(
      {103, ntl::net::scatter_view::from_contiguous(second), true},
      sink, ntl::pool_tag("rTdN"));
  if (!buffered ||
      buffered->state != ntl::ndis::tcp_reassembly_state::buffered)
    return STATUS_INTERNAL_ERROR;
  auto drained = reassembler.push(
      {100, ntl::net::scatter_view::from_contiguous(first), false},
      sink, ntl::pool_tag("rTdN"));
  if (!drained || !reassembler.finished() || delivered != 6)
    return STATUS_INTERNAL_ERROR;
  return ntl::status::ok();
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status reassembly = exercise_reassembly_contract();
  if (!reassembly.is_ok())
    return reassembly;

  auto registration =
      ntl::ndis::lightweight_filter<monitor_module>::try_create(
          driver.native_handle(),
          {L"NTL NDIS compile-contract filter",
           L"{E6913D73-9F37-4988-8FF2-58F26A204C42}",
           L"crtsys_ndis_compile_contracts", 1, 0});
  if (!registration)
    return registration.status();
  driver.on_unload([registration = *registration] {
    registration->reset();
  });
  return ntl::status::ok();
}
