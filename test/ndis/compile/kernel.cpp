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

  void on_pause(ntl::ndis::pause_event) noexcept {
    running_.store(false, std::memory_order_release);
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
    auto metadata = event.try_preserve_metadata();
    if (metadata) {
      (void)metadata->for_each(
          [](ntl::ndis::mutable_net_buffer_list_view packet) noexcept {
            const auto current = packet.view();
            packet.set_checksum(current.checksum().native_value());
            packet.set_large_send(current.large_send().native_value());
            packet.set_vlan(current.vlan().native_value());
            packet.set_receive_hash(current.receive_hash());
            return true;
          });
    }
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
    auto metadata = event.try_preserve_metadata();
    if (metadata) {
      (void)metadata->for_each(
          [](ntl::ndis::mutable_net_buffer_list_view packet) noexcept {
            const auto current = packet.view();
            packet.set_receive_segment_coalescing(
                current.receive_segment_coalescing().native_value());
            return true;
          });
    }
  }

  void on_receive_return(
      ntl::ndis::return_receive_event event) noexcept {
    restored_.fetch_add(
        event.restored_metadata_count(), std::memory_order_relaxed);
  }

  void on_oid_request(
      ntl::ndis::oid_request_event event) noexcept {
    const auto request = event.request();
    (void)request.oid();
    (void)request.method_id();
    (void)request.supported_revision();
    (void)request.type();
    (void)request.port();
    (void)request.request_id();
    (void)request.information_buffer();
    (void)request.input_buffer_length();
    (void)request.output_buffer_length();
    oid_requests_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_oid_request_complete(
      ntl::ndis::oid_request_complete_event event) noexcept {
    const auto request = event.request();
    (void)request.bytes_read();
    (void)request.bytes_written();
    (void)request.bytes_needed();
    (void)event.status();
    oid_completions_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_cancel_oid_request(
      ntl::ndis::cancel_oid_request_event event) noexcept {
    (void)event.request_id();
    oid_cancellations_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_direct_oid_request(
      ntl::ndis::direct_oid_request_event event) noexcept {
    (void)event.request().oid();
    direct_oid_requests_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_direct_oid_request_complete(
      ntl::ndis::direct_oid_request_complete_event event) noexcept {
    (void)event.request().bytes_written();
    (void)event.status();
    direct_oid_completions_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_cancel_direct_oid_request(
      ntl::ndis::cancel_direct_oid_request_event event) noexcept {
    (void)event.request_id();
    direct_oid_cancellations_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_status(ntl::ndis::status_indication_event event) noexcept {
    (void)event.native_handle();
    (void)event.code();
    (void)event.port();
    (void)event.flags();
    (void)event.request_id();
    (void)event.buffer();
    status_indications_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_device_pnp(ntl::ndis::device_pnp_event event) noexcept {
    (void)event.native_handle();
    (void)event.code();
    (void)event.port();
    (void)event.information();
    device_pnp_events_.fetch_add(1, std::memory_order_relaxed);
  }

  void on_net_pnp(ntl::ndis::net_pnp_event event) noexcept {
    (void)event.native_handle();
    (void)event.code();
    (void)event.port();
    (void)event.flags();
    (void)event.information();
    net_pnp_events_.fetch_add(1, std::memory_order_relaxed);
  }

private:
  NET_IFINDEX interface_index_ = 0;
  std::size_t mac_address_length_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> sends_{0};
  std::atomic<std::uint64_t> completions_{0};
  std::atomic<std::uint64_t> receives_{0};
  std::atomic<std::uint64_t> restored_{0};
  std::atomic<std::uint64_t> oid_requests_{0};
  std::atomic<std::uint64_t> oid_completions_{0};
  std::atomic<std::uint64_t> oid_cancellations_{0};
  std::atomic<std::uint64_t> direct_oid_requests_{0};
  std::atomic<std::uint64_t> direct_oid_completions_{0};
  std::atomic<std::uint64_t> direct_oid_cancellations_{0};
  std::atomic<std::uint64_t> status_indications_{0};
  std::atomic<std::uint64_t> device_pnp_events_{0};
  std::atomic<std::uint64_t> net_pnp_events_{0};
};

static_assert(!std::is_copy_constructible_v<
              ntl::ndis::cloned_net_buffer_list>);
static_assert(std::is_trivially_copyable_v<
              ntl::ndis::net_buffer_list_chain_view>);
static_assert(std::is_constructible_v<
              monitor_module, ntl::ndis::attach_event>);
static_assert(ntl::ndis::detail::receive_returns_immediately(
    NDIS_RECEIVE_FLAGS_RESOURCES));
static_assert(!ntl::ndis::detail::receive_returns_immediately(0));

ntl::status exercise_lwf_edge_contracts() noexcept {
  bool restored = false;
  bool notified = false;
  ULONG returned_flags = 0;
  ntl::ndis::detail::complete_immediate_receive(
      NDIS_RECEIVE_FLAGS_RESOURCES |
          NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL,
      [&restored]() noexcept {
        restored = true;
        return std::size_t{3};
      },
      [&notified, &returned_flags](
          ULONG flags, std::size_t count) noexcept {
        notified = count == 3;
        returned_flags = flags;
      });
  if (!restored || !notified ||
      (returned_flags & NDIS_RETURN_FLAGS_DISPATCH_LEVEL) == 0)
    return STATUS_INTERNAL_ERROR;

  int phase = 0;
  void *const request_id = &phase;
  ntl::ndis::detail::forward_oid_cancellation(
      request_id,
      [&phase, request_id](void *observed) noexcept {
        if (observed == request_id && phase == 0)
          phase = 1;
      },
      [&phase, request_id](void *forwarded) noexcept {
        if (forwarded == request_id && phase == 1)
          phase = 2;
      });
  if (phase != 2)
    return STATUS_INTERNAL_ERROR;

  NDIS_OID_REQUEST original{};
  NDIS_OID_REQUEST clone{};
  int oid_phase = 0;
  const NDIS_STATUS forwarded = ntl::ndis::detail::forward_oid_clone(
      &clone,
      [&oid_phase, &clone](NDIS_OID_REQUEST *observed) noexcept {
        if (observed == &clone && oid_phase == 0)
          oid_phase = 1;
      },
      [&oid_phase, &clone](NDIS_OID_REQUEST *submitted) noexcept {
        if (submitted == &clone && oid_phase == 1)
          oid_phase = 2;
        return NDIS_STATUS_SUCCESS;
      },
      [&oid_phase, &clone, &original](
          NDIS_OID_REQUEST *completed, NDIS_STATUS status) noexcept {
        ntl::ndis::detail::complete_oid_clone(
            completed, status,
            [&oid_phase, &clone](
                NDIS_OID_REQUEST *observed,
                NDIS_STATUS observed_status) noexcept {
              if (observed == &clone &&
                  observed_status == NDIS_STATUS_SUCCESS &&
                  oid_phase == 2)
                oid_phase = 3;
            },
            [&oid_phase, &original](NDIS_OID_REQUEST *) noexcept {
              if (oid_phase == 3)
                oid_phase = 4;
              return &original;
            },
            [&oid_phase, &original](
                NDIS_OID_REQUEST *released,
                NDIS_STATUS released_status) noexcept {
              if (released == &original &&
                  released_status == NDIS_STATUS_SUCCESS &&
                  oid_phase == 4)
                oid_phase = 5;
            });
      });
  if (forwarded != NDIS_STATUS_PENDING || oid_phase != 5)
    return STATUS_INTERNAL_ERROR;

  NDIS_OID_REQUEST pending_clone{};
  int pending_phase = 0;
  const NDIS_STATUS pending = ntl::ndis::detail::forward_oid_clone(
      &pending_clone,
      [&pending_phase, &pending_clone](
          NDIS_OID_REQUEST *observed) noexcept {
        if (observed == &pending_clone && pending_phase == 0)
          pending_phase = 1;
      },
      [&pending_phase, &pending_clone](
          NDIS_OID_REQUEST *submitted) noexcept {
        if (submitted == &pending_clone && pending_phase == 1)
          pending_phase = 2;
        return NDIS_STATUS_PENDING;
      },
      [&pending_phase](NDIS_OID_REQUEST *, NDIS_STATUS) noexcept {
        pending_phase = -1;
      });
  return pending == NDIS_STATUS_PENDING && pending_phase == 2
             ? ntl::status::ok()
             : ntl::status{STATUS_INTERNAL_ERROR};
}

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

  reassembler.reset(200);
  delivered = 0;
  auto lost_tail = reassembler.push(
      {206, ntl::net::scatter_view::from_contiguous(second), false},
      sink, ntl::pool_tag("rTdN"));
  auto lost_head = reassembler.push(
      {200, ntl::net::scatter_view::from_contiguous(first), false},
      sink, ntl::pool_tag("rTdN"));
  if (!lost_tail || !lost_head || delivered != 3 ||
      reassembler.buffered_bytes() != second.size())
    return STATUS_INTERNAL_ERROR;
  auto retransmitted_gap = reassembler.push(
      {203, ntl::net::scatter_view::from_contiguous(first), false},
      sink, ntl::pool_tag("rTdN"));
  if (!retransmitted_gap || delivered != 9 ||
      reassembler.buffered_bytes() != 0)
    return STATUS_INTERNAL_ERROR;
  return ntl::status::ok();
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status edge_contracts = exercise_lwf_edge_contracts();
  if (!edge_contracts.is_ok())
    return edge_contracts;
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
