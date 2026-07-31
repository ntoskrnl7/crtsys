#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include <ntl/driver>
#include <ntl/event>
#include <ntl/net/buffer/owned_bytes>
#include <ntl/net/framing>
#include <ntl/passive_executor>
#include <ntl/rpc/server>
#include <ntl/system_thread>
#include <ntl/wfp/all>

#include "tcp_content_filter_contract.hpp"

namespace {

using flow_layer_v4 = wfp_tcp_content_filter::flow_layer_v4;
using flow_layer_v6 = wfp_tcp_content_filter::flow_layer_v6;
using stream_layer_v4 = wfp_tcp_content_filter::stream_layer_v4;
using stream_layer_v6 = wfp_tcp_content_filter::stream_layer_v6;

enum class tcp_flow_phase : std::uint8_t {
  idle,
  deferred,
  permit_ready,
  block_ready,
  closed,
};

class spin_lock_guard {
public:
  explicit spin_lock_guard(KSPIN_LOCK &lock) noexcept : lock_(&lock) {
    KeAcquireSpinLock(lock_, &old_irql_);
  }
  spin_lock_guard(const spin_lock_guard &) = delete;
  spin_lock_guard &operator=(const spin_lock_guard &) = delete;
  ~spin_lock_guard() { KeReleaseSpinLock(lock_, old_irql_); }

private:
  KSPIN_LOCK *lock_;
  KIRQL old_irql_{};
};

struct tcp_flow {
  tcp_flow(std::uint16_t source, std::uint16_t destination) noexcept
      : source_port(source), destination_port(destination) {
    KeInitializeSpinLock(&lock);
  }

  KSPIN_LOCK lock{};
  tcp_flow_phase phase = tcp_flow_phase::idle;
  std::uint64_t request_id = 0;
  std::size_t frame_size = 0;
  std::optional<ntl::wfp::stream_continuation> continuation;
  std::uint16_t source_port = 0;
  std::uint16_t destination_port = 0;
};

struct tcp_flow_context {
  explicit tcp_flow_context(std::shared_ptr<tcp_flow> value) noexcept
      : flow(std::move(value)) {}
  std::shared_ptr<tcp_flow> flow;
};

struct pending_tcp_message {
  pending_tcp_message(std::uint64_t request_id_value,
                      std::uint64_t deadline_value,
                      std::shared_ptr<tcp_flow> flow_value,
                      ntl::net::owned_bytes &&frame_value) noexcept
      : request_id(request_id_value), deadline(deadline_value),
        flow(std::move(flow_value)), frame(std::move(frame_value)) {}

  std::uint64_t request_id = 0;
  std::uint64_t deadline = 0;
  std::shared_ptr<tcp_flow> flow;
  ntl::net::owned_bytes frame;
};

std::uint64_t current_time_100ns() noexcept {
  return static_cast<std::uint64_t>(KeQueryUnbiasedInterruptTime());
}

class tcp_content_filter_state {
public:
  tcp_content_filter_state() noexcept : executor_(DelayedWorkQueue, "TfWN") {
    KeInitializeSpinLock(&pending_lock_);
    ExInitializeRundownProtection(&publish_rundown_);
  }

  tcp_content_filter_state(const tcp_content_filter_state &) = delete;
  tcp_content_filter_state &
  operator=(const tcp_content_filter_state &) = delete;

  ntl::status start_timeout_thread() noexcept {
    auto thread = ntl::system_thread::create(
        &tcp_content_filter_state::timeout_entry, this);
    if (!thread)
      return thread.status();
    timeout_thread_ = std::move(*thread);
    return ntl::status::ok();
  }

  void endpoint(ntl::rpc::server *value) noexcept { endpoint_ = value; }

  bool accepting() const noexcept {
    return accepting_.load(std::memory_order_acquire);
  }

  void stream_target(
      ntl::wfp::flow_target<stream_layer_v4, tcp_flow_context> value) noexcept {
    stream_target_v4_ = std::move(value);
  }

  const std::optional<
      ntl::wfp::flow_target<stream_layer_v4, tcp_flow_context>> &
  stream_target_v4() const noexcept {
    return stream_target_v4_;
  }

  void stream_target(
      ntl::wfp::flow_target<stream_layer_v6, tcp_flow_context> value) noexcept {
    stream_target_v6_ = std::move(value);
  }

  const std::optional<
      ntl::wfp::flow_target<stream_layer_v6, tcp_flow_context>> &
  stream_target_v6() const noexcept {
    return stream_target_v6_;
  }

  ntl::status defer(const std::shared_ptr<tcp_flow> &flow,
                    ntl::net::owned_bytes &&frame,
                    ntl::wfp::stream_continuation continuation,
                    std::size_t frame_size) noexcept {
    if (!accepting() || !flow || !continuation ||
        active_session_.load(std::memory_order_acquire) == 0 ||
        !reserve_pending())
      return STATUS_DEVICE_NOT_READY;

    const std::uint64_t request_id =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<pending_tcp_message> pending;
    try {
      pending = std::make_shared<pending_tcp_message>(
          request_id,
          current_time_100ns() + wfp_tcp_content_filter::verdict_timeout_100ns,
          flow, std::move(frame));
    } catch (...) {
      release_pending();
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!insert_pending(pending)) {
      release_pending();
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    {
      spin_lock_guard guard(flow->lock);
      if (flow->phase != tcp_flow_phase::idle) {
        (void)take_pending(request_id);
        return STATUS_INVALID_DEVICE_STATE;
      }
      flow->phase = tcp_flow_phase::deferred;
      flow->request_id = request_id;
      flow->frame_size = frame_size;
      flow->continuation = continuation;
    }

    if (!ExAcquireRundownProtection(&publish_rundown_)) {
      (void)take_pending(request_id);
      close_flow(flow);
      return STATUS_DELETE_PENDING;
    }
    const ntl::status queued = executor_.post([this, request_id]() noexcept {
      publish(request_id);
      ExReleaseRundownProtection(&publish_rundown_);
    });
    if (!queued.is_ok()) {
      ExReleaseRundownProtection(&publish_rundown_);
      (void)take_pending(request_id);
      close_flow(flow);
    }
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
        static_cast<wfp_tcp_content_filter::wire_verdict>(wire_value);
    if (verdict != wfp_tcp_content_filter::wire_verdict::permit &&
        verdict != wfp_tcp_content_filter::wire_verdict::block)
      return static_cast<std::int32_t>(STATUS_INVALID_PARAMETER);

    auto pending = take_pending(request_id);
    if (!pending)
      return static_cast<std::int32_t>(STATUS_NOT_FOUND);

    const ntl::status completed = complete(
        *pending, verdict == wfp_tcp_content_filter::wire_verdict::permit
                      ? tcp_flow_phase::permit_ready
                      : tcp_flow_phase::block_ready);
    if (!completed.is_ok()) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      return static_cast<std::int32_t>(static_cast<NTSTATUS>(completed));
    }
    if (verdict == wfp_tcp_content_filter::wire_verdict::permit)
      permitted_.fetch_add(1, std::memory_order_relaxed);
    else
      blocked_.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(STATUS_SUCCESS);
  }

  wfp_tcp_content_filter_stats stats() const noexcept {
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
  }

  static void close_flow(const std::shared_ptr<tcp_flow> &flow) noexcept {
    if (!flow)
      return;
    spin_lock_guard guard(flow->lock);
    flow->phase = tcp_flow_phase::closed;
    flow->request_id = 0;
    flow->frame_size = 0;
    flow->continuation.reset();
  }

private:
  static void NTAPI timeout_entry(void *context) noexcept {
    static_cast<tcp_content_filter_state *>(context)->timeout_loop();
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

  void publish(std::uint64_t request_id) noexcept {
    auto pending = find_pending(request_id);
    if (!pending)
      return;

    const std::uint64_t session_id =
        active_session_.load(std::memory_order_acquire);
    if (!accepting() || !endpoint_ || session_id == 0 ||
        pending->frame.size() <
            wfp_tcp_content_filter::sample_u32_be_prefix_size ||
        pending->frame.size() > wfp_tcp_content_filter::maximum_frame_size) {
      expire_now(request_id);
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    wfp_tcp_content_request request;
    request.id = request_id;
    request.source_port = pending->flow->source_port;
    request.destination_port = pending->flow->destination_port;
    request.content_offset = static_cast<std::uint32_t>(
        wfp_tcp_content_filter::sample_u32_be_prefix_size);
    request.content_size = static_cast<std::uint32_t>(
        pending->frame.size() -
        wfp_tcp_content_filter::sample_u32_be_prefix_size);
    try {
      request.frame.resize(pending->frame.size());
      std::memcpy(request.frame.data(), pending->frame.data(),
                  pending->frame.size());
    } catch (...) {
      expire_now(request_id);
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    const ntl::status notified = endpoint_->try_notify(
        session_id, wfp_tcp_content_filter::inspection_requests, request);
    if (!notified.is_ok()) {
      expire_now(request_id);
      failed_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    queued_.fetch_add(1, std::memory_order_relaxed);
  }

  bool reserve_pending() noexcept {
    std::size_t current = pending_count_.load(std::memory_order_relaxed);
    while (current < wfp_tcp_content_filter::maximum_pending_requests) {
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

  bool
  insert_pending(const std::shared_ptr<pending_tcp_message> &pending) noexcept {
    spin_lock_guard guard(pending_lock_);
    for (auto &slot : pending_) {
      if (!slot) {
        slot = pending;
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<pending_tcp_message>
  find_pending(std::uint64_t request_id) noexcept {
    spin_lock_guard guard(pending_lock_);
    for (const auto &slot : pending_) {
      if (slot && slot->request_id == request_id)
        return slot;
    }
    return {};
  }

  std::shared_ptr<pending_tcp_message>
  take_pending(std::uint64_t request_id) noexcept {
    std::shared_ptr<pending_tcp_message> result;
    {
      spin_lock_guard guard(pending_lock_);
      for (auto &slot : pending_) {
        if (slot && slot->request_id == request_id) {
          result = std::move(slot);
          break;
        }
      }
    }
    if (result)
      release_pending();
    return result;
  }

  std::shared_ptr<pending_tcp_message>
  take_expired(std::uint64_t now) noexcept {
    std::shared_ptr<pending_tcp_message> result;
    {
      spin_lock_guard guard(pending_lock_);
      for (auto &slot : pending_) {
        if (slot && slot->deadline <= now) {
          result = std::move(slot);
          break;
        }
      }
    }
    if (result)
      release_pending();
    return result;
  }

  std::shared_ptr<pending_tcp_message> take_first() noexcept {
    std::shared_ptr<pending_tcp_message> result;
    {
      spin_lock_guard guard(pending_lock_);
      for (auto &slot : pending_) {
        if (slot) {
          result = std::move(slot);
          break;
        }
      }
    }
    if (result)
      release_pending();
    return result;
  }

  void expire_now(std::uint64_t request_id) noexcept {
    spin_lock_guard guard(pending_lock_);
    for (auto &slot : pending_) {
      if (slot && slot->request_id == request_id) {
        slot->deadline = 0;
        return;
      }
    }
  }

  static ntl::status complete(pending_tcp_message &pending,
                              tcp_flow_phase verdict) noexcept {
    std::optional<ntl::wfp::stream_continuation> continuation;
    {
      spin_lock_guard guard(pending.flow->lock);
      if (pending.flow->phase != tcp_flow_phase::deferred ||
          pending.flow->request_id != pending.request_id ||
          !pending.flow->continuation)
        return STATUS_NOT_FOUND;
      pending.flow->phase = verdict;
      continuation = pending.flow->continuation;
    }
    const ntl::status continued = continuation->resume();
    if (!continued.is_ok())
      close_flow(pending.flow);
    return continued;
  }

  void expire_pending(std::uint64_t now) noexcept {
    for (;;) {
      auto expired = take_expired(now);
      if (!expired)
        break;
      const ntl::status completed =
          complete(*expired, tcp_flow_phase::block_ready);
      if (!completed.is_ok())
        failed_.fetch_add(1, std::memory_order_relaxed);
      timed_out_.fetch_add(1, std::memory_order_relaxed);
      blocked_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void drop_all_pending() noexcept {
    for (;;) {
      auto pending = take_first();
      if (!pending)
        break;
      const ntl::status completed =
          complete(*pending, tcp_flow_phase::block_ready);
      if (!completed.is_ok())
        failed_.fetch_add(1, std::memory_order_relaxed);
      blocked_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ntl::passive_executor executor_;
  ntl::rpc::server *endpoint_ = nullptr;
  EX_RUNDOWN_REF publish_rundown_{};
  KSPIN_LOCK pending_lock_{};
  std::array<std::shared_ptr<pending_tcp_message>,
             wfp_tcp_content_filter::maximum_pending_requests>
      pending_{};
  std::optional<ntl::wfp::flow_target<stream_layer_v4, tcp_flow_context>>
      stream_target_v4_;
  std::optional<ntl::wfp::flow_target<stream_layer_v6, tcp_flow_context>>
      stream_target_v6_;
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

tcp_content_filter_state *g_state = nullptr;

template <class StreamLayer>
ntl::wfp::stream_result
inspect_tcp(const ntl::wfp::stream_event<StreamLayer, tcp_flow_context>
                &event) noexcept {
  const auto data = event.data();
  tcp_flow_context *const context = event.context();
  const std::shared_ptr<tcp_flow> flow =
      context ? context->flow : std::shared_ptr<tcp_flow>{};
  tcp_content_filter_state *const state = g_state;
  if (!state || !state->accepting() || !flow)
    return ntl::wfp::stream_result::drop_connection();

  // WFP permits DEFER only for inbound stream data. Outbound traffic is
  // outside this sample's contract and passes unchanged.
  if ((data.flags() & FWPS_STREAM_FLAG_RECEIVE) == 0)
    return ntl::wfp::stream_result::permit(data.size());

  {
    spin_lock_guard guard(flow->lock);
    if (flow->phase == tcp_flow_phase::permit_ready) {
      if (flow->frame_size == 0 || flow->frame_size > data.size()) {
        flow->phase = tcp_flow_phase::closed;
        return ntl::wfp::stream_result::drop_connection();
      }
      const std::size_t frame_size = flow->frame_size;
      flow->phase = tcp_flow_phase::idle;
      flow->request_id = 0;
      flow->frame_size = 0;
      flow->continuation.reset();
      return ntl::wfp::stream_result::permit(frame_size);
    }
    if (flow->phase == tcp_flow_phase::block_ready ||
        flow->phase == tcp_flow_phase::deferred ||
        flow->phase == tcp_flow_phase::closed) {
      flow->phase = tcp_flow_phase::closed;
      return ntl::wfp::stream_result::drop_connection();
    }
  }

  if (event.missed_bytes() != 0 || event.buffer_limit_reached()) {
    tcp_content_filter_state::close_flow(flow);
    return ntl::wfp::stream_result::drop_connection();
  }
  if (data.empty()) {
    if (event.no_more_data())
      return ntl::wfp::stream_result::permit(0);
    return ntl::wfp::stream_result::need_more(static_cast<std::uint32_t>(
        wfp_tcp_content_filter::sample_u32_be_prefix_size));
  }

  const ntl::net::framing::u32_be_length_prefix framer{
      wfp_tcp_content_filter::maximum_content_size};
  const auto probe = ntl::net::framing::validate(
      framer.probe(data.bytes()), data.size(),
      ntl::net::framing::frame_limits{
          wfp_tcp_content_filter::maximum_frame_size});
  if (probe.state() == ntl::net::framing::probe_state::malformed ||
      (probe.state() == ntl::net::framing::probe_state::need_more &&
       event.no_more_data())) {
    tcp_content_filter_state::close_flow(flow);
    return ntl::wfp::stream_result::drop_connection();
  }
  if (probe.state() == ntl::net::framing::probe_state::need_more) {
    const std::size_t additional = probe.required_total() > data.size()
                                       ? probe.required_total() - data.size()
                                       : 1;
    return ntl::wfp::stream_result::need_more(
        static_cast<std::uint32_t>(additional));
  }

  auto frame_view = data.bytes().subview(0, probe.frame_size());
  if (!frame_view) {
    tcp_content_filter_state::close_flow(flow);
    return ntl::wfp::stream_result::drop_connection();
  }
  auto frame = ntl::net::owned_bytes::try_copy(
      *frame_view,
      ntl::net::buffer_limits{wfp_tcp_content_filter::maximum_frame_size},
      ntl::pool_tag("tTfW"));
  const auto continuation = event.continuation();
  if (!frame || !continuation) {
    tcp_content_filter_state::close_flow(flow);
    return ntl::wfp::stream_result::drop_connection();
  }

  const ntl::status deferred =
      state->defer(flow, std::move(*frame), continuation, probe.frame_size());
  if (!deferred.is_ok()) {
    tcp_content_filter_state::close_flow(flow);
    return ntl::wfp::stream_result::drop_connection();
  }
  return ntl::wfp::stream_result::defer();
}

template <class FlowLayer, class StreamLayer>
ntl::wfp::decision
begin_tcp_flow(const ntl::wfp::classify_event<FlowLayer> &event) noexcept {
  tcp_content_filter_state *const state = g_state;
  if (!state || !state->accepting())
    return ntl::wfp::decision::continue_classification;

  auto &target = [&]() -> auto & {
    if constexpr (std::is_same_v<StreamLayer, stream_layer_v4>)
      return state->stream_target_v4();
    else
      return state->stream_target_v6();
  }();
  if (!target)
    return ntl::wfp::decision::continue_classification;

  const auto handle = event.metadata().flow_handle();
  const auto protocol = event.value(FlowLayer::field::protocol).uint8();
  const auto direction = event.value(FlowLayer::field::direction).uint32();
  const auto source_port = event.value(FlowLayer::field::remote_port).uint16();
  const auto destination_port =
      event.value(FlowLayer::field::local_port).uint16();
  if (!handle || !protocol || !direction || !source_port || !destination_port ||
      *protocol != IPPROTO_TCP || *direction != FWP_DIRECTION_INBOUND)
    return ntl::wfp::decision::continue_classification;

  try {
    auto flow = std::make_shared<tcp_flow>(*source_port, *destination_port);
    std::unique_ptr<tcp_flow_context> context(
        new (std::nothrow) tcp_flow_context(std::move(flow)));
    if (context)
      (void)target->associate(*handle, std::move(context));
  } catch (...) {
  }
  return ntl::wfp::decision::continue_classification;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<tcp_content_filter_state>();

  ntl::rpc::server_options options(wfp_tcp_content_filter::endpoint_name);
  options.contract_version(wfp_tcp_content_filter::contract_version)
      .capabilities(wfp_tcp_content_filter::capabilities::current)
      .asynchronous()
      .max_pending_calls(32)
      .max_pending_notifications(16)
      .max_sessions(4)
      .max_reliable_notifications_per_session(static_cast<std::uint32_t>(
          wfp_tcp_content_filter::maximum_pending_requests))
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
      .register_notification(wfp_tcp_content_filter::inspection_requests)
      .on(wfp_tcp_content_filter::submit_verdict,
          [state](const ntl::rpc::call_context &context,
                  std::uint64_t request_id, std::uint8_t verdict) {
            return state->submit_verdict(context, request_id, verdict);
          })
      .on(wfp_tcp_content_filter::query_stats,
          [state] { return state->stats(); });
  server->start();

  auto callouts = std::make_shared<ntl::wfp::callout_driver<>>(driver);
  g_state = state.get();

  auto stream_target_v4 =
      callouts->add_stream<tcp_flow_context, inspect_tcp<stream_layer_v4>>(
          wfp_tcp_content_filter::stream_callout_key_v4);
  if (!stream_target_v4) {
    state->stop_accepting();
    g_state = nullptr;
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return stream_target_v4.status();
  }
  state->stream_target(*stream_target_v4);

  auto stream_target_v6 =
      callouts->add_stream<tcp_flow_context, inspect_tcp<stream_layer_v6>>(
          wfp_tcp_content_filter::stream_callout_key_v6);
  if (!stream_target_v6) {
    state->stop_accepting();
    g_state = nullptr;
    (void)callouts->reset();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return stream_target_v6.status();
  }
  state->stream_target(*stream_target_v6);

  const ntl::status flow_registered_v4 =
      callouts->add<begin_tcp_flow<flow_layer_v4, stream_layer_v4>>(
          wfp_tcp_content_filter::flow_callout_key_v4);
  if (!flow_registered_v4.is_ok()) {
    state->stop_accepting();
    g_state = nullptr;
    (void)callouts->reset();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return flow_registered_v4;
  }
  const ntl::status flow_registered_v6 =
      callouts->add<begin_tcp_flow<flow_layer_v6, stream_layer_v6>>(
          wfp_tcp_content_filter::flow_callout_key_v6);
  if (!flow_registered_v6.is_ok()) {
    state->stop_accepting();
    g_state = nullptr;
    (void)callouts->reset();
    state->wait_for_publishers();
    server.reset();
    state->shutdown_resources();
    return flow_registered_v6;
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
