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
  std::atomic<std::uint64_t> metadata_preserved{0};
  std::atomic<std::uint64_t> metadata_restored{0};
  std::atomic<std::uint64_t> oid_requests{0};
  std::atomic<std::uint64_t> oid_completions{0};
  std::atomic<std::uint64_t> oid_cancellations{0};
  std::atomic<std::uint64_t> direct_oid_requests{0};
  std::atomic<std::uint64_t> direct_oid_completions{0};
  std::atomic<std::uint64_t> direct_oid_cancellations{0};
  std::atomic<std::uint64_t> status_indications{0};
  std::atomic<std::uint64_t> device_pnp_events{0};
  std::atomic<std::uint64_t> net_pnp_events{0};
  std::atomic<std::uint64_t> immediate_receive_returns{0};

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
        metadata_preserved.load(std::memory_order_relaxed),
        metadata_restored.load(std::memory_order_relaxed),
        oid_requests.load(std::memory_order_relaxed),
        oid_completions.load(std::memory_order_relaxed),
        oid_cancellations.load(std::memory_order_relaxed),
        direct_oid_requests.load(std::memory_order_relaxed),
        direct_oid_completions.load(std::memory_order_relaxed),
        direct_oid_cancellations.load(std::memory_order_relaxed),
        status_indications.load(std::memory_order_relaxed),
        device_pnp_events.load(std::memory_order_relaxed),
        net_pnp_events.load(std::memory_order_relaxed),
        immediate_receive_returns.load(std::memory_order_relaxed),
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

  void on_pause(ntl::ndis::pause_event) noexcept {
    if (state_)
      state_->pauses.fetch_add(1, std::memory_order_relaxed);
  }

  void on_send(ntl::ndis::send_event event) noexcept {
    if (!state_)
      return;
    state_->send_lists.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    state_->send_bytes.fetch_add(
        event.packets().byte_count(), std::memory_order_relaxed);
    observe_metadata(event.packets());
    preserve_metadata(event);
  }

  void on_send_complete(
      ntl::ndis::send_complete_event event) noexcept {
    if (state_) {
      state_->send_completions.fetch_add(
          event.packets().list_count(), std::memory_order_relaxed);
      state_->metadata_restored.fetch_add(
          event.restored_metadata_count(), std::memory_order_relaxed);
    }
  }

  void on_receive(ntl::ndis::receive_event event) noexcept {
    if (!state_)
      return;
    state_->receive_lists.fetch_add(
        event.packets().list_count(), std::memory_order_relaxed);
    state_->receive_bytes.fetch_add(
        event.packets().byte_count(), std::memory_order_relaxed);
    observe_metadata(event.packets());
    preserve_metadata(event);
  }

  void on_receive_return(
      ntl::ndis::return_receive_event event) noexcept {
    if (state_) {
      state_->metadata_restored.fetch_add(
          event.restored_metadata_count(), std::memory_order_relaxed);
      if (event.immediate())
        state_->immediate_receive_returns.fetch_add(
            event.packets().list_count(), std::memory_order_relaxed);
    }
  }

  void on_oid_request(
      ntl::ndis::oid_request_event event) noexcept {
    (void)event.request().oid();
    if (state_)
      state_->oid_requests.fetch_add(1, std::memory_order_relaxed);
  }

  void on_oid_request_complete(
      ntl::ndis::oid_request_complete_event event) noexcept {
    (void)event.status();
    if (state_)
      state_->oid_completions.fetch_add(1, std::memory_order_relaxed);
  }

  void on_cancel_oid_request(
      ntl::ndis::cancel_oid_request_event event) noexcept {
    (void)event.request_id();
    if (state_)
      state_->oid_cancellations.fetch_add(1, std::memory_order_relaxed);
  }

  void on_direct_oid_request(
      ntl::ndis::direct_oid_request_event event) noexcept {
    (void)event.request().oid();
    if (state_)
      state_->direct_oid_requests.fetch_add(
          1, std::memory_order_relaxed);
  }

  void on_direct_oid_request_complete(
      ntl::ndis::direct_oid_request_complete_event event) noexcept {
    (void)event.status();
    if (state_)
      state_->direct_oid_completions.fetch_add(
          1, std::memory_order_relaxed);
  }

  void on_cancel_direct_oid_request(
      ntl::ndis::cancel_direct_oid_request_event event) noexcept {
    (void)event.request_id();
    if (state_)
      state_->direct_oid_cancellations.fetch_add(
          1, std::memory_order_relaxed);
  }

  void on_status(ntl::ndis::status_indication_event event) noexcept {
    (void)event.code();
    (void)event.buffer();
    if (state_)
      state_->status_indications.fetch_add(1, std::memory_order_relaxed);
  }

  void on_device_pnp(ntl::ndis::device_pnp_event event) noexcept {
    (void)event.code();
    (void)event.information();
    if (state_)
      state_->device_pnp_events.fetch_add(1, std::memory_order_relaxed);
  }

  void on_net_pnp(ntl::ndis::net_pnp_event event) noexcept {
    (void)event.code();
    (void)event.information();
    if (state_)
      state_->net_pnp_events.fetch_add(1, std::memory_order_relaxed);
  }

private:
  template <class Event>
  void preserve_metadata(Event event) noexcept {
    auto metadata = event.try_preserve_metadata(
        ntl::pool_tag("mMwN"));
    if (!metadata || !state_)
      return;
    state_->metadata_preserved.fetch_add(
        metadata->list_count(), std::memory_order_relaxed);
  }

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

  ntl::ndis::bounded_tcp_reassembler<4> loss(
      100, {.maximum_buffered_bytes = 16,
            .maximum_forward_window = 16});
  std::size_t loss_delivered = 0;
  auto loss_sink = [&loss_delivered](
                       ntl::net::scatter_view data) noexcept -> ntl::status {
    loss_delivered += data.size();
    return ntl::status::ok();
  };
  auto loss_tail = loss.push(
      {106, ntl::net::scatter_view::from_contiguous(tail), false},
      loss_sink, ntl::pool_tag("rMwN"));
  auto loss_head = loss.push(
      {100, ntl::net::scatter_view::from_contiguous(tail), false},
      loss_sink, ntl::pool_tag("rMwN"));
  if (!loss_tail || !loss_head || loss_delivered != 3 ||
      loss.buffered_bytes() != tail.size())
    return STATUS_INTERNAL_ERROR;
  auto retransmitted_gap = loss.push(
      {103, ntl::net::scatter_view::from_contiguous(tail), false},
      loss_sink, ntl::pool_tag("rMwN"));
  if (!retransmitted_gap || loss_delivered != 9 ||
      loss.buffered_bytes() != 0)
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

ntl::status validate_metadata_restoration_contract() noexcept {
  NET_BUFFER_LIST list{};
  std::byte original_checksum{};
  std::byte completed_checksum{};
  std::byte original_large_send{};
  std::byte completed_large_send{};

  NET_BUFFER_LIST_INFO(
      &list, TcpIpChecksumNetBufferListInfo) = &original_checksum;
  NET_BUFFER_LIST_INFO(
      &list, TcpLargeSendNetBufferListInfo) = &original_large_send;
  const auto snapshot =
      ntl::ndis::detail::nbl_metadata_snapshot::capture(&list);

  NET_BUFFER_LIST_INFO(
      &list, TcpIpChecksumNetBufferListInfo) = &completed_checksum;
  NET_BUFFER_LIST_INFO(
      &list, TcpLargeSendNetBufferListInfo) = &completed_large_send;
  snapshot.restore(
      &list,
      ntl::ndis::detail::nbl_metadata_snapshot::checksum_field |
          ntl::ndis::detail::nbl_metadata_snapshot::large_send_field,
      ntl::ndis::detail::nbl_metadata_snapshot::restore_point::send_complete);
  if (NET_BUFFER_LIST_INFO(
          &list, TcpIpChecksumNetBufferListInfo) != &original_checksum ||
      NET_BUFFER_LIST_INFO(
          &list, TcpLargeSendNetBufferListInfo) != &completed_large_send)
    return STATUS_INTERNAL_ERROR;

  snapshot.restore(
      &list,
      ntl::ndis::detail::nbl_metadata_snapshot::large_send_field,
      ntl::ndis::detail::nbl_metadata_snapshot::restore_point::receive_return);
  return NET_BUFFER_LIST_INFO(
             &list, TcpLargeSendNetBufferListInfo) == &original_large_send
             ? ntl::status::ok()
             : ntl::status{STATUS_INTERNAL_ERROR};
}

ntl::status validate_nbl_view_contract() noexcept {
  NET_BUFFER_LIST first_list{};
  NET_BUFFER_LIST second_list{};
  NET_BUFFER first_buffer{};
  NET_BUFFER second_buffer{};
  NET_BUFFER_LIST_FIRST_NB(&first_list) = &first_buffer;
  NET_BUFFER_LIST_FIRST_NB(&second_list) = &second_buffer;
  NET_BUFFER_LIST_NEXT_NBL(&first_list) = &second_list;
  NET_BUFFER_DATA_LENGTH(&first_buffer) = 3;
  NET_BUFFER_DATA_LENGTH(&second_buffer) = 5;

  ntl::ndis::net_buffer_list_chain_view chain(&first_list);
  std::array<std::size_t, 2> sizes{};
  std::size_t index = 0;
  (void)chain.for_each(
      [&sizes, &index](ntl::ndis::net_buffer_list_view list) noexcept {
        if (index < sizes.size())
          sizes[index++] = list.byte_count();
        return true;
      });
  return chain.list_count() == 2 && chain.byte_count() == 8 &&
                 index == 2 && sizes[0] == 3 && sizes[1] == 5
             ? ntl::status::ok()
             : ntl::status{STATUS_INTERNAL_ERROR};
}

ntl::status validate_lwf_edge_contracts() noexcept {
  bool restored = false;
  bool notified = false;
  ULONG returned_flags = 0;
  ntl::ndis::detail::complete_immediate_receive(
      NDIS_RECEIVE_FLAGS_RESOURCES |
          NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL,
      [&restored]() noexcept {
        restored = true;
        return std::size_t{2};
      },
      [&notified, &returned_flags](
          ULONG flags, std::size_t count) noexcept {
        notified = count == 2;
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

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  const ntl::status reassembly = validate_reassembly_contract();
  if (!reassembly.is_ok())
    return reassembly;
  const ntl::status metadata = validate_metadata_restoration_contract();
  if (!metadata.is_ok())
    return metadata;
  const ntl::status nbl_view = validate_nbl_view_contract();
  if (!nbl_view.is_ok())
    return nbl_view;
  const ntl::status edge_contracts = validate_lwf_edge_contracts();
  if (!edge_contracts.is_ok())
    return edge_contracts;

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
