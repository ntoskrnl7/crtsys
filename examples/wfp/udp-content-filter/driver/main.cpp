#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
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
                   std::uint64_t deadline_value, std::uint64_t endpoint_value,
                   std::uint32_t compartment_value, ADDRESS_FAMILY family_value,
                   std::array<UINT8, 16> remote_address_value) noexcept
      : packet(std::move(packet_value)), deadline(deadline_value),
        endpoint(endpoint_value), compartment(compartment_value),
        family(family_value), remote_address(remote_address_value) {}

  ntl::wfp::cloned_packet packet;
  std::uint64_t deadline = 0;
  std::uint64_t endpoint = 0;
  std::uint32_t compartment = 0;
  ADDRESS_FAMILY family = AF_UNSPEC;
  std::array<UINT8, 16> remote_address{};
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

  FWPS_PACKET_INJECTION_STATE
  query(ntl::wfp::borrowed_packet packet,
        ADDRESS_FAMILY family) const noexcept {
    return family == AF_INET ? injector_v4_.query(packet)
                             : injector_v6_.query(packet);
  }

  ntl::status defer(ntl::wfp::cloned_packet &&packet,
                    ntl::net::owned_bytes &&payload, std::uint16_t source_port,
                    std::uint16_t destination_port, std::uint64_t endpoint,
                    std::uint32_t compartment, ADDRESS_FAMILY family,
                    std::array<UINT8, 16> remote_address) noexcept {
    if (!accepting() || !ExAcquireRundownProtection(&publish_rundown_))
      return STATUS_DELETE_PENDING;

    const std::uint64_t request_id =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    const ntl::status queued = executor_.post(
        [this, request_id, packet = std::move(packet),
         payload = std::move(payload), source_port, destination_port, endpoint,
         compartment, family, remote_address]() mutable noexcept {
          publish(request_id, std::move(packet), std::move(payload),
                  source_port, destination_port, endpoint, compartment, family,
                  remote_address);
          ExReleaseRundownProtection(&publish_rundown_);
        });
    if (!queued.is_ok())
      ExReleaseRundownProtection(&publish_rundown_);
    return queued;
  }

  NTSTATUS open_session(std::uint64_t session_id) noexcept {
    std::uint64_t expected = 0;
    if (active_session_.compare_exchange_strong(expected, session_id,
                                                std::memory_order_acq_rel))
      return STATUS_SUCCESS;
    return expected == session_id ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
  }

  void disconnect_session(std::uint64_t session_id) noexcept {
    std::uint64_t expected = session_id;
    if (active_session_.compare_exchange_strong(expected, 0,
                                                std::memory_order_acq_rel))
      drop_all_pending();
  }

  std::int32_t submit_verdict(const ntl::rpc::call_context &context,
                              std::uint64_t request_id,
                              std::uint8_t wire_value) noexcept {
    auto *const session = context.session();
    if (!session ||
        active_session_.load(std::memory_order_acquire) != session->id())
      return static_cast<std::int32_t>(STATUS_ACCESS_DENIED);

    const auto verdict =
        static_cast<wfp_udp_content_filter::wire_verdict>(wire_value);
    if (verdict != wfp_udp_content_filter::wire_verdict::permit &&
        verdict != wfp_udp_content_filter::wire_verdict::block)
      return static_cast<std::int32_t>(STATUS_INVALID_PARAMETER);

    auto pending = take_pending(request_id);
    if (!pending)
      return static_cast<std::int32_t>(STATUS_NOT_FOUND);

    if (verdict == wfp_udp_content_filter::wire_verdict::block) {
      blocked_.fetch_add(1, std::memory_order_relaxed);
      return static_cast<std::int32_t>(STATUS_SUCCESS);
    }

    FWPS_TRANSPORT_SEND_PARAMS0 parameters{};
    parameters.remoteAddress = pending->remote_address.data();
    auto &injector = pending->family == AF_INET ? injector_v4_ : injector_v6_;
    const ntl::status injected = injector.inject_send(
        std::move(pending->packet), pending->endpoint, pending->family,
        static_cast<COMPARTMENT_ID>(pending->compartment), &parameters);
    if (!injected.is_ok()) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return static_cast<std::int32_t>(static_cast<NTSTATUS>(injected));
    }
    permitted_.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(STATUS_SUCCESS);
  }

  wfp_udp_content_filter_stats stats() const noexcept {
    return {
        queued_.load(std::memory_order_relaxed),
        permitted_.load(std::memory_order_relaxed),
        blocked_.load(std::memory_order_relaxed),
        timed_out_.load(std::memory_order_relaxed),
        failed_.load(std::memory_order_relaxed),
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
               ntl::net::owned_bytes &&payload, std::uint16_t source_port,
               std::uint16_t destination_port, std::uint64_t endpoint,
               std::uint32_t compartment, ADDRESS_FAMILY family,
               std::array<UINT8, 16> remote_address) noexcept {
    if (!accepting() || !endpoint_) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    const std::uint64_t session_id =
        active_session_.load(std::memory_order_acquire);
    if (session_id == 0) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    wfp_udp_content_request request;
    request.id = request_id;
    request.source_port = source_port;
    request.destination_port = destination_port;
    try {
      request.payload.resize(payload.size());
      if (!request.payload.empty())
        std::memcpy(request.payload.data(), payload.data(), payload.size());
    } catch (...) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    std::unique_ptr<pending_datagram> pending(
        new (std::nothrow)
            pending_datagram(std::move(packet),
                             current_time_100ns() +
                                 wfp_udp_content_filter::verdict_timeout_100ns,
                             endpoint, compartment, family, remote_address));
    if (!pending || !reserve_pending()) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    try {
      fast_mutex_guard guard(pending_lock_);
      if (active_session_.load(std::memory_order_acquire) != session_id) {
        release_pending();
        failed_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      const auto inserted = pending_.emplace(request_id, std::move(pending));
      if (!inserted.second) {
        release_pending();
        failed_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
    } catch (...) {
      release_pending();
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    const ntl::status notified = endpoint_->try_notify(
        session_id, wfp_udp_content_filter::inspection_requests, request);
    if (!notified.is_ok()) {
      (void)take_pending(request_id);
      failed_.fetch_add(1, std::memory_order_relaxed);
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
      removed.swap(pending_);
    }
    for (std::size_t index = 0; index != removed.size(); ++index)
      release_pending();
    blocked_.fetch_add(static_cast<std::uint64_t>(removed.size()),
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
  std::atomic<std::uint64_t> failed_{0};
};

udp_content_filter_state *g_state = nullptr;

template <class Layer>
ntl::wfp::decision
inspect_udp(const ntl::wfp::classify_event<Layer> &event) noexcept {
  udp_content_filter_state *const state = g_state;
  const auto packet = event.packet();
  if (!state || !state->accepting() || !packet)
    return ntl::wfp::decision::block_and_absorb;

  constexpr ADDRESS_FAMILY family =
      std::is_same_v<Layer, layer_v4> ? AF_INET : AF_INET6;
  const auto injection_state = state->query(packet, family);
  if (injection_state == FWPS_PACKET_INJECTED_BY_SELF ||
      injection_state == FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
    return ntl::wfp::decision::permit;

  const auto direction = event.value(Layer::field::direction).uint32();
  const auto protocol = event.value(Layer::field::protocol).uint8();
  const auto source_port = event.value(Layer::field::local_port).uint16();
  const auto destination_port = event.value(Layer::field::remote_port).uint16();
  const auto endpoint = event.metadata().transport_endpoint_handle();
  const auto compartment = event.metadata().compartment_id();
  if (!direction || !protocol || !source_port || !destination_port ||
      !endpoint || !compartment || *direction != FWP_DIRECTION_OUTBOUND ||
      *protocol != IPPROTO_UDP)
    return ntl::wfp::decision::block_and_absorb;

  std::array<UINT8, 16> remote_address{};
  if constexpr (std::is_same_v<Layer, layer_v4>) {
    const auto address = event.value(Layer::field::remote_address).uint32();
    if (!address)
      return ntl::wfp::decision::block_and_absorb;
    const std::uint32_t network_order = RtlUlongByteSwap(*address);
    std::memcpy(remote_address.data(), &network_order, sizeof(network_order));
  } else {
    const auto *address =
        event.value(Layer::field::remote_address).byte_array16();
    if (!address)
      return ntl::wfp::decision::block_and_absorb;
    std::memcpy(remote_address.data(), address->byteArray16,
                remote_address.size());
  }

  NET_BUFFER_LIST *const list = packet.native_handle();
  if (!list || NET_BUFFER_LIST_NEXT_NBL(list) ||
      !NET_BUFFER_LIST_FIRST_NB(list) ||
      NET_BUFFER_NEXT_NB(NET_BUFFER_LIST_FIRST_NB(list)))
    return ntl::wfp::decision::block_and_absorb;

  constexpr std::size_t udp_header_size = 8;
  const auto bytes = packet.bytes();
  if (!bytes || bytes.size() < udp_header_size)
    return ntl::wfp::decision::block_and_absorb;
  ntl::net::byte_cursor cursor(bytes);
  const auto header_source = cursor.read_be16();
  const auto header_destination = cursor.read_be16();
  const auto udp_length = cursor.read_be16();
  const auto checksum = cursor.read_be16();
  (void)checksum;
  if (!header_source || !header_destination || !udp_length ||
      *header_source != *source_port ||
      *header_destination != *destination_port ||
      *udp_length < udp_header_size || *udp_length > bytes.size())
    return ntl::wfp::decision::block_and_absorb;

  const std::size_t payload_size =
      static_cast<std::size_t>(*udp_length) - udp_header_size;
  if (payload_size > wfp_udp_content_filter::maximum_payload_size)
    return ntl::wfp::decision::block_and_absorb;
  auto payload_view = bytes.subview(udp_header_size, payload_size);
  if (!payload_view)
    return ntl::wfp::decision::block_and_absorb;
  auto payload = ntl::net::owned_bytes::try_copy(
      *payload_view,
      ntl::net::buffer_limits{wfp_udp_content_filter::maximum_payload_size},
      ntl::pool_tag("pUfW"));
  auto clone = ntl::wfp::cloned_packet::try_create(packet);
  if (!payload || !clone)
    return ntl::wfp::decision::block_and_absorb;

  // A delayed transport clone must not rely on the original send's
  // checksum-offload context. The transport injection path rebuilds the
  // checksum for both address families.
  const ntl::status checksum_cleared = clone->edit_bytes().write_be16(6, 0);
  if (!checksum_cleared.is_ok())
    return ntl::wfp::decision::block_and_absorb;

  const ntl::status deferred = state->defer(
      std::move(*clone), std::move(*payload), *source_port, *destination_port,
      *endpoint, *compartment, family, remote_address);
  (void)deferred;
  return ntl::wfp::decision::block_and_absorb;
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

  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);
  g_state = state.get();
  const ntl::status registered_v4 = callouts->add<inspect_udp<layer_v4>>(
      wfp_udp_content_filter::callout_key_v4);
  if (!registered_v4.is_ok()) {
    state->stop_accepting();
    g_state = nullptr;
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return registered_v4;
  }
  const ntl::status registered_v6 = callouts->add<inspect_udp<layer_v6>>(
      wfp_udp_content_filter::callout_key_v6);
  if (!registered_v6.is_ok()) {
    state->stop_accepting();
    g_state = nullptr;
    (void)callouts->reset();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return registered_v6;
  }

  const ntl::status thread_started = state->start_timeout_thread();
  if (!thread_started.is_ok()) {
    state->stop_accepting();
    g_state = nullptr;
    (void)callouts->reset();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return thread_started;
  }

  driver.on_unload([state, server, callouts]() mutable {
    state->stop_accepting();
    state->fail_all_pending();
    const ntl::status reset = callouts->reset();
    NT_ASSERT(reset.is_ok());
    g_state = nullptr;
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
  });
  return ntl::status::ok();
}
