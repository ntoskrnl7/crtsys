#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <ntl/driver>
#include <ntl/event>
#include <ntl/net/buffer/owned_bytes>
#include <ntl/passive_executor>
#include <ntl/rpc/server>
#include <ntl/system_thread>
#include <ntl/wfp/all>

#include "udp_content_filter_contract.hpp"

namespace {

using layer_v4 = wfp_udp_content_filter::layer_v4;
using layer_v6 = wfp_udp_content_filter::layer_v6;

class fast_mutex_guard {
public:
  explicit fast_mutex_guard(FAST_MUTEX &mutex) noexcept : mutex_(&mutex) {
    ExAcquireFastMutex(mutex_);
  }
  fast_mutex_guard(const fast_mutex_guard &) = delete;
  fast_mutex_guard &operator=(const fast_mutex_guard &) = delete;
  ~fast_mutex_guard() { ExReleaseFastMutex(mutex_); }

private:
  FAST_MUTEX *mutex_;
};

struct pending_datagram {
  pending_datagram(ntl::wfp::cloned_packet &&packet_value,
                    ntl::wfp::transport_send_request &&send_request_value,
                    std::uint64_t session_id_value,
                    std::uint64_t deadline_value) noexcept
      : packet(std::move(packet_value)),
        send_request(std::move(send_request_value)),
        session_id(session_id_value), deadline(deadline_value) {}

  ntl::wfp::cloned_packet packet;
  ntl::wfp::transport_send_request send_request;
  std::uint64_t session_id = 0;
  std::uint64_t deadline = 0;
};

std::uint64_t current_time_100ns() noexcept {
  return static_cast<std::uint64_t>(KeQueryUnbiasedInterruptTime());
}

class udp_content_filter_state {
public:
  explicit udp_content_filter_state(
      ntl::wfp::transport_injector &&injector_v4,
      ntl::wfp::transport_injector &&injector_v6) noexcept
      : injector_v4_(std::move(injector_v4)),
        injector_v6_(std::move(injector_v6)),
        executor_(DelayedWorkQueue, "UfWN") {
    ExInitializeFastMutex(&pending_lock_);
    ExInitializeRundownProtection(&publish_rundown_);
  }

  udp_content_filter_state(const udp_content_filter_state &) = delete;
  udp_content_filter_state &
  operator=(const udp_content_filter_state &) = delete;

  ntl::status start_timeout_thread() noexcept {
    auto thread = ntl::system_thread::create(
        &udp_content_filter_state::timeout_entry, this);
    if (!thread)
      return thread.status();
    timeout_thread_ = std::move(*thread);
    return ntl::status::ok();
  }

  void endpoint(ntl::rpc::server *value) noexcept { endpoint_ = value; }

  bool accepting() const noexcept {
    return accepting_.load(std::memory_order_acquire);
  }

  void record_malformed() noexcept {
    malformed_.fetch_add(1, std::memory_order_relaxed);
    blocked_.fetch_add(1, std::memory_order_relaxed);
  }

  void record_failure() noexcept {
    failed_.fetch_add(1, std::memory_order_relaxed);
    blocked_.fetch_add(1, std::memory_order_relaxed);
  }

  FWPS_PACKET_INJECTION_STATE
  query(ntl::wfp::borrowed_packet packet,
        ADDRESS_FAMILY family) const noexcept {
    return family == AF_INET ? injector_v4_.query(packet)
                             : injector_v6_.query(packet);
  }

  ntl::status defer(ntl::wfp::cloned_packet &&packet,
                     ntl::net::owned_bytes &&payload,
                     ntl::wfp::transport_send_request &&send_request,
                     std::uint16_t source_port,
                     std::uint16_t destination_port) noexcept {
    const std::uint64_t session_id =
        active_session_.load(std::memory_order_acquire);
    if (!accepting() || session_id == 0 || !send_request)
      return STATUS_DELETE_PENDING;
    if (!reserve_pending())
      return STATUS_QUOTA_EXCEEDED;
    if (!ExAcquireRundownProtection(&publish_rundown_)) {
      release_pending();
      return STATUS_DELETE_PENDING;
    }

    const std::uint64_t request_id =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    const ADDRESS_FAMILY family = send_request.family();
    const ntl::status queued = executor_.post(
        [this, request_id, packet = std::move(packet),
         payload = std::move(payload), source_port, destination_port, family,
         send_request = std::move(send_request), session_id]() mutable noexcept {
          publish(request_id, std::move(packet), std::move(payload),
                  std::move(send_request), source_port, destination_port,
                  family, session_id);
          ExReleaseRundownProtection(&publish_rundown_);
        });
    if (!queued.is_ok()) {
      release_pending();
      ExReleaseRundownProtection(&publish_rundown_);
    }
    return queued;
  }

  NTSTATUS open_session(std::uint64_t session_id) noexcept {
    if (session_id == 0 || !accepting())
      return STATUS_DELETE_PENDING;
    fast_mutex_guard guard(pending_lock_);
    if (!accepting())
      return STATUS_DELETE_PENDING;
    const std::uint64_t active =
        active_session_.load(std::memory_order_relaxed);
    if (active == 0) {
      active_session_.store(session_id, std::memory_order_release);
      return STATUS_SUCCESS;
    }
    return active == session_id ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
  }

  void disconnect_session(std::uint64_t session_id) noexcept {
    std::unordered_map<std::uint64_t, std::unique_ptr<pending_datagram>>
        removed;
    {
      fast_mutex_guard guard(pending_lock_);
      if (active_session_.load(std::memory_order_relaxed) != session_id)
        return;
      active_session_.store(0, std::memory_order_release);
      removed.swap(pending_);
    }
    cancel_removed(removed);
  }

  std::int32_t submit_verdict(const ntl::rpc::call_context &context,
                              std::uint64_t request_id,
                              std::uint8_t wire_value) noexcept {
    auto *const session = context.session();
    if (!session)
      return static_cast<std::int32_t>(STATUS_ACCESS_DENIED);

    const auto verdict =
        static_cast<wfp_udp_content_filter::wire_verdict>(wire_value);
    if (verdict != wfp_udp_content_filter::wire_verdict::permit &&
        verdict != wfp_udp_content_filter::wire_verdict::block &&
        verdict != wfp_udp_content_filter::wire_verdict::malformed)
      return static_cast<std::int32_t>(STATUS_INVALID_PARAMETER);

    NTSTATUS take_status = STATUS_UNSUCCESSFUL;
    auto pending =
        take_pending(request_id, session->id(), take_status);
    if (!pending)
      return static_cast<std::int32_t>(take_status);

    if (verdict != wfp_udp_content_filter::wire_verdict::permit) {
      blocked_.fetch_add(1, std::memory_order_relaxed);
      if (verdict == wfp_udp_content_filter::wire_verdict::malformed)
        malformed_.fetch_add(1, std::memory_order_relaxed);
      return static_cast<std::int32_t>(STATUS_SUCCESS);
    }

    auto &injector = pending->send_request.family() == AF_INET ? injector_v4_
                                                               : injector_v6_;
    const ntl::status injected = injector.inject_send(
        ntl::wfp::transport_send_packet(
            std::move(pending->packet), std::move(pending->send_request)));
    if (!injected.is_ok()) {
      record_failure();
      return static_cast<std::int32_t>(static_cast<NTSTATUS>(injected));
    }
    permitted_.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(STATUS_SUCCESS);
  }

  wfp_udp_content_filter_stats stats() const noexcept {
    const auto ipv4_injection = injector_v4_.statistics();
    const auto ipv6_injection = injector_v6_.statistics();
    const NTSTATUS last_injection_status =
        !NT_SUCCESS(ipv6_injection.last_completion_status)
            ? ipv6_injection.last_completion_status
            : ipv4_injection.last_completion_status;
    return {
        queued_.load(std::memory_order_relaxed),
        permitted_.load(std::memory_order_relaxed),
        blocked_.load(std::memory_order_relaxed),
        timed_out_.load(std::memory_order_relaxed),
        cancelled_.load(std::memory_order_relaxed),
        malformed_.load(std::memory_order_relaxed),
        failed_.load(std::memory_order_relaxed),
        ipv4_injection.completion_failures +
            ipv6_injection.completion_failures,
        static_cast<std::uint32_t>(last_injection_status),
    };
  }

  void stop_accepting() noexcept {
    accepting_.store(false, std::memory_order_release);
  }

  void fail_all_pending() noexcept { drop_all_pending(); }

  void wait_for_publishers() noexcept {
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExWaitForRundownProtectionRelease(&publish_rundown_);
  }

  void shutdown_resources() noexcept {
    stop_event_.set();
    if (timeout_thread_) {
      const ntl::status joined = timeout_thread_.join();
      NT_ASSERT(joined.is_ok());
    }
    drop_all_pending();
    injector_v4_.reset();
    injector_v6_.reset();
  }

private:
  static void NTAPI timeout_entry(void *context) noexcept {
    static_cast<udp_content_filter_state *>(context)->timeout_loop();
    PsTerminateSystemThread(STATUS_SUCCESS);
  }

  void timeout_loop() noexcept {
    for (;;) {
      LARGE_INTEGER interval{};
      interval.QuadPart = -500000; // 50 ms in relative 100-ns units.
      const ntl::status waited = stop_event_.wait(&interval);
      if (static_cast<NTSTATUS>(waited) == STATUS_SUCCESS)
        return;
      if (static_cast<NTSTATUS>(waited) != STATUS_TIMEOUT)
        return;
      expire_pending(current_time_100ns());
    }
  }

  void publish(std::uint64_t request_id, ntl::wfp::cloned_packet &&packet,
                ntl::net::owned_bytes &&payload,
                ntl::wfp::transport_send_request &&send_request,
                std::uint16_t source_port,
                std::uint16_t destination_port, ADDRESS_FAMILY family,
                std::uint64_t session_id) noexcept {
    if (!accepting() || !endpoint_) {
      fail_reserved();
      return;
    }

    if (session_id == 0 ||
        active_session_.load(std::memory_order_acquire) != session_id) {
      fail_reserved();
      return;
    }

    wfp_udp_content_request request;
    request.id = request_id;
    request.address_family = static_cast<std::uint16_t>(family);
    request.source_port = source_port;
    request.destination_port = destination_port;
    try {
      request.payload.resize(payload.size());
      if (!request.payload.empty())
        std::memcpy(request.payload.data(), payload.data(), payload.size());
    } catch (...) {
      fail_reserved();
      return;
    }

    std::unique_ptr<pending_datagram> pending(
        new (std::nothrow)
            pending_datagram(std::move(packet), std::move(send_request),
                             session_id,
                             current_time_100ns() +
                                 wfp_udp_content_filter::verdict_timeout_100ns));
    if (!pending) {
      fail_reserved();
      return;
    }

    try {
      fast_mutex_guard guard(pending_lock_);
      if (active_session_.load(std::memory_order_acquire) != session_id) {
        fail_reserved();
        return;
      }
      const auto inserted = pending_.emplace(request_id, std::move(pending));
      if (!inserted.second) {
        fail_reserved();
        return;
      }
    } catch (...) {
      fail_reserved();
      return;
    }

    const ntl::status notified = endpoint_->try_notify(
        session_id, wfp_udp_content_filter::inspection_requests, request);
    if (!notified.is_ok()) {
      (void)take_pending(request_id);
      record_failure();
      return;
    }
    queued_.fetch_add(1, std::memory_order_relaxed);
  }

  std::unique_ptr<pending_datagram>
  take_pending(std::uint64_t request_id) noexcept {
    std::unique_ptr<pending_datagram> result;
    {
      fast_mutex_guard guard(pending_lock_);
      const auto found = pending_.find(request_id);
      if (found == pending_.end())
        return {};
      result = std::move(found->second);
      pending_.erase(found);
    }
    release_pending();
    return result;
  }

  std::unique_ptr<pending_datagram>
  take_pending(std::uint64_t request_id, std::uint64_t session_id,
               NTSTATUS &status) noexcept {
    std::unique_ptr<pending_datagram> result;
    {
      fast_mutex_guard guard(pending_lock_);
      if (active_session_.load(std::memory_order_relaxed) != session_id) {
        status = STATUS_ACCESS_DENIED;
        return {};
      }
      const auto found = pending_.find(request_id);
      if (found == pending_.end()) {
        status = STATUS_NOT_FOUND;
        return {};
      }
      if (found->second->session_id != session_id) {
        status = STATUS_ACCESS_DENIED;
        return {};
      }
      result = std::move(found->second);
      pending_.erase(found);
      status = STATUS_SUCCESS;
    }
    release_pending();
    return result;
  }

  bool reserve_pending() noexcept {
    std::size_t current = pending_count_.load(std::memory_order_relaxed);
    while (current < wfp_udp_content_filter::maximum_pending_requests) {
      if (pending_count_.compare_exchange_weak(current, current + 1,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
        return true;
    }
    return false;
  }

  void release_pending() noexcept {
    const std::size_t previous =
        pending_count_.fetch_sub(1, std::memory_order_acq_rel);
    NT_ASSERT(previous != 0);
    (void)previous;
  }

  void fail_reserved() noexcept {
    release_pending();
    record_failure();
  }

  void expire_pending(std::uint64_t now) noexcept {
    for (;;) {
      std::unique_ptr<pending_datagram> expired;
      {
        fast_mutex_guard guard(pending_lock_);
        for (auto iterator = pending_.begin(); iterator != pending_.end();
             ++iterator) {
          if (iterator->second->deadline <= now) {
            expired = std::move(iterator->second);
            pending_.erase(iterator);
            break;
          }
        }
      }
      if (!expired)
        break;
      release_pending();
      timed_out_.fetch_add(1, std::memory_order_relaxed);
      blocked_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void drop_all_pending() noexcept {
    std::unordered_map<std::uint64_t, std::unique_ptr<pending_datagram>>
        removed;
    {
      fast_mutex_guard guard(pending_lock_);
      active_session_.store(0, std::memory_order_release);
      removed.swap(pending_);
    }
    cancel_removed(removed);
  }

  void cancel_removed(
      const std::unordered_map<std::uint64_t,
                               std::unique_ptr<pending_datagram>> &removed)
      noexcept {
    for (std::size_t index = 0; index != removed.size(); ++index)
      release_pending();
    blocked_.fetch_add(static_cast<std::uint64_t>(removed.size()),
                       std::memory_order_relaxed);
    cancelled_.fetch_add(static_cast<std::uint64_t>(removed.size()),
                         std::memory_order_relaxed);
  }

  ntl::wfp::transport_injector injector_v4_;
  ntl::wfp::transport_injector injector_v6_;
  ntl::passive_executor executor_;
  ntl::rpc::server *endpoint_ = nullptr;
  EX_RUNDOWN_REF publish_rundown_{};
  FAST_MUTEX pending_lock_{};
  std::unordered_map<std::uint64_t, std::unique_ptr<pending_datagram>> pending_;
  ntl::event stop_event_;
  ntl::system_thread timeout_thread_;
  std::atomic<bool> accepting_{true};
  std::atomic<std::uint64_t> active_session_{0};
  std::atomic<std::uint64_t> next_request_id_{1};
  std::atomic<std::size_t> pending_count_{0};
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> permitted_{0};
  std::atomic<std::uint64_t> blocked_{0};
  std::atomic<std::uint64_t> timed_out_{0};
  std::atomic<std::uint64_t> cancelled_{0};
  std::atomic<std::uint64_t> malformed_{0};
  std::atomic<std::uint64_t> failed_{0};
};

template <class Layer>
ntl::wfp::terminating_decision
inspect_udp(udp_content_filter_state &owned_state,
            const ntl::wfp::classify_event<Layer> &event) noexcept {
  udp_content_filter_state *const state = &owned_state;
  const auto packet = event.packet();
  if (!state->accepting() || !packet) {
    state->record_failure();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  constexpr ADDRESS_FAMILY family =
      std::is_same_v<Layer, layer_v4> ? AF_INET : AF_INET6;
  const auto injection_state = state->query(packet, family);
  if (injection_state == FWPS_PACKET_INJECTED_BY_SELF ||
      injection_state == FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
    return ntl::wfp::terminating_decision::permit;

  const auto direction = event.value(Layer::field::direction).uint32();
  const auto protocol = event.value(Layer::field::protocol).uint8();
  const auto source_port = event.value(Layer::field::local_port).uint16();
  const auto destination_port = event.value(Layer::field::remote_port).uint16();
  const auto endpoint = event.metadata().transport_endpoint_handle();
  const auto compartment = event.metadata().compartment_id();
  if (!direction || !protocol) {
    state->record_failure();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  if (*direction != FWP_DIRECTION_OUTBOUND || *protocol != IPPROTO_UDP)
    return ntl::wfp::terminating_decision::permit;
  if (!source_port || !destination_port || !endpoint || !compartment) {
    state->record_failure();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  std::array<UINT8, 16> remote_address{};
  if constexpr (std::is_same_v<Layer, layer_v4>) {
    const auto address = event.value(Layer::field::remote_address).uint32();
    if (!address) {
      state->record_failure();
      return ntl::wfp::terminating_decision::block_and_absorb;
    }
    const std::uint32_t network_order = RtlUlongByteSwap(*address);
    std::memcpy(remote_address.data(), &network_order, sizeof(network_order));
  } else {
    const auto *address =
        event.value(Layer::field::remote_address).byte_array16();
    if (!address) {
      state->record_failure();
      return ntl::wfp::terminating_decision::block_and_absorb;
    }
    std::memcpy(remote_address.data(), address->byteArray16,
                remote_address.size());
  }

  NET_BUFFER_LIST *const list = packet.borrowed_native_handle();
  if (!list || NET_BUFFER_LIST_NEXT_NBL(list) ||
      !NET_BUFFER_LIST_FIRST_NB(list) ||
      NET_BUFFER_NEXT_NB(NET_BUFFER_LIST_FIRST_NB(list))) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  constexpr std::size_t udp_header_size = 8;
  const auto bytes = packet.bytes();
  if (!bytes || bytes.size() < udp_header_size) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  ntl::net::borrowed_byte_cursor cursor(bytes);
  const auto header_source = cursor.read_be16();
  const auto header_destination = cursor.read_be16();
  const auto udp_length = cursor.read_be16();
  const auto checksum = cursor.read_be16();
  (void)checksum;
  if (!header_source || !header_destination || !udp_length ||
      *header_source != *source_port ||
      *header_destination != *destination_port ||
      *udp_length < udp_header_size || *udp_length != bytes.size()) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  const std::size_t payload_size =
      static_cast<std::size_t>(*udp_length) - udp_header_size;
  if (payload_size > wfp_udp_content_filter::maximum_record_size) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  auto payload_view = bytes.subview(udp_header_size, payload_size);
  if (!payload_view) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  auto payload = ntl::net::owned_bytes::try_copy(
      *payload_view,
      ntl::net::buffer_limits{wfp_udp_content_filter::maximum_record_size},
      ntl::pool_tag("pUfW"));
  const auto control_view = event.metadata().transport_control_data();
  if (!control_view ||
      control_view->size() >
          wfp_udp_content_filter::maximum_transport_control_data_size) {
    state->record_malformed();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }
  auto clone = ntl::wfp::cloned_packet::try_create(packet);
  if (!payload || !clone) {
    state->record_failure();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  const SCOPE_ID remote_scope =
      event.metadata().remote_scope_id().value_or(SCOPE_ID{});
  const std::size_t remote_address_size =
      family == AF_INET ? sizeof(std::uint32_t) : remote_address.size();
  auto send_request = ntl::wfp::transport_send_request::try_copy(
      *endpoint, family, static_cast<COMPARTMENT_ID>(*compartment),
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(remote_address.data()),
          remote_address_size),
      remote_scope, *control_view,
      ntl::net::buffer_limits{
          wfp_udp_content_filter::maximum_transport_control_data_size},
      ntl::pool_tag("cUfW"));
  if (!send_request) {
    state->record_failure();
    return ntl::wfp::terminating_decision::block_and_absorb;
  }

  const ntl::status deferred = state->defer(
      std::move(*clone), std::move(*payload), std::move(*send_request),
      *source_port, *destination_port);
  if (!deferred.is_ok())
    state->record_failure();
  return ntl::wfp::terminating_decision::block_and_absorb;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto injector_v4 = ntl::wfp::transport_injector::try_create(AF_INET);
  if (!injector_v4)
    return injector_v4.status();
  auto injector_v6 = ntl::wfp::transport_injector::try_create(AF_INET6);
  if (!injector_v6)
    return injector_v6.status();

  auto state = std::make_shared<udp_content_filter_state>(
      std::move(*injector_v4), std::move(*injector_v6));

  ntl::rpc::server_options options(wfp_udp_content_filter::endpoint_name);
  options.contract_version(wfp_udp_content_filter::contract_version)
      .capabilities(wfp_udp_content_filter::capabilities::current)
      .asynchronous()
      .max_pending_calls(32)
      .max_pending_notifications(16)
      .max_sessions(4)
      .max_reliable_notifications_per_session(static_cast<std::uint32_t>(
          wfp_udp_content_filter::maximum_pending_requests))
      .max_reliable_notifications(128)
      .session_retention_ms(2000);

  auto server = ntl::rpc::make_server(driver, options);
  state->endpoint(server.get());
  server
      ->on_session_open([state](ntl::rpc::client_session &session,
                                const ntl::rpc::call_context &) {
        return state->open_session(session.id());
      })
      .on_session_resume([state](ntl::rpc::client_session &session,
                                 const ntl::rpc::call_context &) {
        return state->open_session(session.id());
      })
      .on_session_disconnect([state](ntl::rpc::client_session &session) {
        state->disconnect_session(session.id());
      })
      .on_session_close([state](ntl::rpc::client_session &session) {
        state->disconnect_session(session.id());
      })
      .register_notification(wfp_udp_content_filter::inspection_requests)
      .on(wfp_udp_content_filter::submit_verdict,
          [state](const ntl::rpc::call_context &context,
                  std::uint64_t request_id, std::uint8_t verdict) {
            return state->submit_verdict(context, request_id, verdict);
          })
      .on(wfp_udp_content_filter::query_stats,
          [state] { return state->stats(); });
  server->start();

  ntl::wfp::callout_driver<> callouts(driver);
  const ntl::status registered_v4 = callouts.add_terminating(
      wfp_udp_content_filter::callout_key_v4, state, &inspect_udp<layer_v4>);
  if (!registered_v4.is_ok()) {
    state->stop_accepting();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return registered_v4;
  }
  const ntl::status registered_v6 = callouts.add_terminating(
      wfp_udp_content_filter::callout_key_v6, state, &inspect_udp<layer_v6>);
  if (!registered_v6.is_ok()) {
    state->stop_accepting();
    (void)callouts.close();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return registered_v6;
  }

  const ntl::status thread_started = state->start_timeout_thread();
  if (!thread_started.is_ok()) {
    state->stop_accepting();
    (void)callouts.close();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return thread_started;
  }

  driver.on_unload([state, server, callouts]() mutable {
    state->stop_accepting();
    state->fail_all_pending();
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
  });
  return ntl::status::ok();
}
