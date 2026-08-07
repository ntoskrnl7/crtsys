#include <ntddk.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include <ntl/driver>
#include <ntl/net/buffer/owned_bytes>
#include <ntl/passive_executor>
#include <ntl/wfp/all>

#include "stream_edit_contract.hpp"

namespace {

using flow_layer = wfp_stream_edit::flow_layer;
using stream_layer = wfp_stream_edit::stream_layer;
inline constexpr std::size_t token_size =
    sizeof(wfp_stream_edit::token) - 1;
inline constexpr std::size_t oob_token_size =
    sizeof(wfp_stream_edit::oob_token) - 1;

struct edit_state;

enum class edit_flow_phase : std::uint8_t {
  idle,
  deferred,
  clone_ready,
  replacement_ready,
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

struct edit_flow {
  explicit edit_flow(edit_state &value) noexcept : state(&value) {
    KeInitializeSpinLock(&lock);
  }

  edit_state *state;
  KSPIN_LOCK lock{};
  edit_flow_phase phase = edit_flow_phase::idle;
  std::size_t deferred_length = 0;
  std::optional<ntl::wfp::stream_injection_site<stream_layer>> site;
  std::optional<ntl::wfp::cloned_stream_data> clone;
  std::optional<ntl::wfp::injected_stream_data> replacement;
};

struct edit_flow_context {
  explicit edit_flow_context(std::shared_ptr<edit_flow> value) noexcept
      : flow(std::move(value)) {}
  std::shared_ptr<edit_flow> flow;
};

struct edit_state {
  explicit edit_state(ntl::wfp::stream_injector &&value) noexcept
      : injector(std::move(value)), executor(DelayedWorkQueue, "eOwW") {
    ExInitializeRundownProtection(&worker_rundown);
  }

  bool accepting() const noexcept {
    return accepting_work.load(std::memory_order_acquire);
  }

  ntl::status defer(
      const std::shared_ptr<edit_flow> &flow,
      ntl::wfp::cloned_stream_data &&clone,
      ntl::net::owned_bytes &&bytes,
      ntl::wfp::stream_injection_site<stream_layer> site) noexcept {
    if (!accepting() || !flow || !clone || !site ||
        bytes.empty() || !pending_budget.try_acquire())
      return STATUS_DEVICE_NOT_READY;

    {
      spin_lock_guard guard(flow->lock);
      if (flow->phase != edit_flow_phase::idle) {
        (void)pending_budget.release();
        return STATUS_INVALID_DEVICE_STATE;
      }
      flow->phase = edit_flow_phase::deferred;
      flow->deferred_length = bytes.size();
      flow->site = site;
    }

    if (!ExAcquireRundownProtection(&worker_rundown)) {
      close_flow(flow);
      (void)pending_budget.release();
      return STATUS_DELETE_PENDING;
    }

    const ntl::status queued = executor.post(
        [this, flow, clone = std::move(clone),
         bytes = std::move(bytes), site]() mutable noexcept {
          process(flow, std::move(clone), bytes, site);
          (void)pending_budget.release();
          ExReleaseRundownProtection(&worker_rundown);
        });
    if (!queued.is_ok()) {
      close_flow(flow);
      (void)pending_budget.release();
      ExReleaseRundownProtection(&worker_rundown);
    }
    return queued;
  }

  void stop() noexcept {
    accepting_work.store(false, std::memory_order_release);
    ExWaitForRundownProtectionRelease(&worker_rundown);
  }

  static void close_flow(
      const std::shared_ptr<edit_flow> &flow) noexcept {
    if (!flow)
      return;
    spin_lock_guard guard(flow->lock);
    flow->phase = edit_flow_phase::closed;
    flow->deferred_length = 0;
    flow->site.reset();
    flow->clone.reset();
    flow->replacement.reset();
  }

  ntl::wfp::stream_injector injector;
  ntl::passive_executor executor;
  EX_RUNDOWN_REF worker_rundown{};
  std::optional<
      ntl::wfp::flow_target<stream_layer, edit_flow_context>>
      target;
  std::atomic<std::uint64_t> replacements{0};
  std::atomic<std::uint64_t> oob_replacements{0};
  std::atomic<std::uint64_t> oob_clones{0};
  std::atomic<std::uint64_t> failures{0};
  wfp_stream_edit::oob_pending_budget pending_budget;
  std::atomic<bool> accepting_work{true};

private:
  void process(
      const std::shared_ptr<edit_flow> &flow,
      ntl::wfp::cloned_stream_data &&clone,
      const ntl::net::owned_bytes &bytes,
      ntl::wfp::stream_injection_site<stream_layer> site) noexcept {
    const bool replace =
        bytes.size() >= oob_token_size &&
        std::memcmp(bytes.data(), wfp_stream_edit::oob_token,
                    oob_token_size) == 0;

    std::optional<ntl::wfp::injected_stream_data> replacement_data;
    if (replace) {
      auto created = injector.try_make_data(
          wfp_stream_edit::oob_replacement,
          sizeof(wfp_stream_edit::oob_replacement) - 1,
          clone.stream_flags());
      if (!created) {
        failures.fetch_add(1, std::memory_order_relaxed);
        close_flow(flow);
        (void)site.continue_deferred();
        return;
      }
      replacement_data.emplace(std::move(*created));
    }

    {
      spin_lock_guard guard(flow->lock);
      if (flow->phase != edit_flow_phase::deferred ||
          !flow->site) {
        failures.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (replace) {
        flow->replacement.emplace(
            std::move(*replacement_data));
        flow->phase = edit_flow_phase::replacement_ready;
      } else {
        flow->clone.emplace(std::move(clone));
        flow->phase = edit_flow_phase::clone_ready;
      }
    }

    const ntl::status continued = site.continue_deferred();
    if (!continued.is_ok()) {
      failures.fetch_add(1, std::memory_order_relaxed);
      close_flow(flow);
      return;
    }
    if (replace)
      oob_replacements.fetch_add(1, std::memory_order_relaxed);
    else
      oob_clones.fetch_add(1, std::memory_order_relaxed);
  }
};

inline constexpr auto token_pattern =
    ntl::net::byte_literal(wfp_stream_edit::token);
inline constexpr auto oob_token_pattern =
    ntl::net::byte_literal(wfp_stream_edit::oob_token);

ntl::wfp::stream_result edit_stream(
    edit_state &,
    const ntl::wfp::stream_event<stream_layer, edit_flow_context> &event)
    noexcept {
      const auto data = event.data();
      edit_flow_context *const context = event.context();
      const std::shared_ptr<edit_flow> flow =
          context ? context->flow : std::shared_ptr<edit_flow>{};
      if (!flow || !flow->state->accepting())
        return ntl::wfp::stream_result::drop_connection();

      if ((data.flags() & FWPS_STREAM_FLAG_RECEIVE) != 0) {
        std::optional<ntl::wfp::cloned_stream_data> ready_clone;
        std::optional<ntl::wfp::injected_stream_data>
            ready_replacement;
        std::size_t deferred_length = 0;
        edit_flow_phase ready_phase = edit_flow_phase::idle;
        {
          spin_lock_guard guard(flow->lock);
          ready_phase = flow->phase;
          if (ready_phase == edit_flow_phase::clone_ready) {
            deferred_length = flow->deferred_length;
            ready_clone.emplace(std::move(*flow->clone));
            flow->clone.reset();
            flow->phase = edit_flow_phase::idle;
            flow->deferred_length = 0;
            flow->site.reset();
          } else if (
              ready_phase == edit_flow_phase::replacement_ready) {
            deferred_length = flow->deferred_length;
            ready_replacement.emplace(
                std::move(*flow->replacement));
            flow->replacement.reset();
            flow->phase = edit_flow_phase::idle;
            flow->deferred_length = 0;
            flow->site.reset();
          }
        }

        if (ready_phase == edit_flow_phase::clone_ready) {
          if (deferred_length == 0 ||
              deferred_length > data.size() || !ready_clone)
            return ntl::wfp::stream_result::drop_connection();
          const ntl::status injected = flow->state->injector.inject(
              event.injection_site(), std::move(*ready_clone));
          if (!injected.is_ok()) {
            flow->state->failures.fetch_add(
                1, std::memory_order_relaxed);
            return ntl::wfp::stream_result::drop_connection();
          }
          return ntl::wfp::stream_result::block(deferred_length);
        }
        if (ready_phase == edit_flow_phase::replacement_ready) {
          if (deferred_length == 0 ||
              deferred_length > data.size() || !ready_replacement)
            return ntl::wfp::stream_result::drop_connection();
          const ntl::status injected = flow->state->injector.inject(
              event.injection_site(), std::move(*ready_replacement));
          if (!injected.is_ok()) {
            flow->state->failures.fetch_add(
                1, std::memory_order_relaxed);
            return ntl::wfp::stream_result::drop_connection();
          }
          return ntl::wfp::stream_result::block(deferred_length);
        }
        if (ready_phase == edit_flow_phase::deferred ||
            ready_phase == edit_flow_phase::closed)
          return ntl::wfp::stream_result::drop_connection();

        if (event.missed_bytes() != 0 ||
            event.buffer_limit_reached() ||
            data.size() > wfp_stream_edit::maximum_oob_bytes) {
          flow->state->failures.fetch_add(
              1, std::memory_order_relaxed);
          edit_state::close_flow(flow);
          return ntl::wfp::stream_result::drop_connection();
        }
        if (data.empty())
          return event.no_more_data()
                     ? ntl::wfp::stream_result::permit(0)
                     : ntl::wfp::stream_result::need_more(1);

        const auto scan =
            ntl::net::scan_bytes(data.bytes(), oob_token_pattern);
        if (!scan) {
          flow->state->failures.fetch_add(
              1, std::memory_order_relaxed);
          return ntl::wfp::stream_result::drop_connection();
        }
        if (scan->found && scan->offset != 0)
          return ntl::wfp::stream_result::permit(scan->offset);
        if (!scan->found && scan->trailing_prefix != 0) {
          if (scan->trailing_prefix != data.size())
            return ntl::wfp::stream_result::permit(
                data.size() - scan->trailing_prefix);
          if (!event.no_more_data())
            return ntl::wfp::stream_result::need_more(
                static_cast<std::uint32_t>(oob_token_size));
        }

        auto clone = ntl::wfp::cloned_stream_data::try_create(data);
        auto bytes = ntl::net::owned_bytes::try_copy(
            data.bytes(),
            ntl::net::buffer_limits{
                wfp_stream_edit::maximum_oob_bytes},
            ntl::pool_tag("bOwW"));
        const auto site = event.injection_site();
        if (!clone || !bytes || !site) {
          flow->state->failures.fetch_add(
              1, std::memory_order_relaxed);
          return ntl::wfp::stream_result::drop_connection();
        }
        const ntl::status deferred = flow->state->defer(
            flow, std::move(*clone), std::move(*bytes), site);
        if (!deferred.is_ok()) {
          flow->state->failures.fetch_add(
              1, std::memory_order_relaxed);
          return ntl::wfp::stream_result::drop_connection();
        }
        return ntl::wfp::stream_result::defer();
      }

      if (data.size() == 0)
        return ntl::wfp::stream_result::permit(0);
      if (event.buffer_limit_reached()) {
        flow->state->failures.fetch_add(
            1, std::memory_order_relaxed);
        return ntl::wfp::stream_result::drop_connection();
      }

      const auto scan =
          ntl::net::scan_bytes(data.bytes(), token_pattern);
      if (!scan) {
        flow->state->failures.fetch_add(
            1, std::memory_order_relaxed);
        return ntl::wfp::stream_result::drop_connection();
      }

      if (scan->found) {
        const std::size_t token_offset = scan->offset;
        if (token_offset != 0)
          return ntl::wfp::stream_result::permit(token_offset);

        auto replacement = flow->state->injector.try_make_data(
            wfp_stream_edit::replacement, token_size, data.flags());
        if (!replacement) {
          flow->state->failures.fetch_add(1, std::memory_order_relaxed);
          return ntl::wfp::stream_result::drop_connection();
        }
        const ntl::status injected = flow->state->injector.inject(
            event.injection_site(), std::move(*replacement));
        if (!injected.is_ok()) {
          flow->state->failures.fetch_add(1, std::memory_order_relaxed);
          return ntl::wfp::stream_result::drop_connection();
        }

        flow->state->replacements.fetch_add(
            1, std::memory_order_relaxed);
        return ntl::wfp::stream_result::block(token_size);
      }

      const std::size_t suffix = scan->trailing_prefix;
      if (event.no_more_data())
        return ntl::wfp::stream_result::permit(data.size());
      if (suffix == data.size())
        return ntl::wfp::stream_result::need_more(
            static_cast<std::uint32_t>(token_size));
      return ntl::wfp::stream_result::permit(data.size() - suffix);
}

void begin_edit_flow(
    edit_state &owned_state,
    const ntl::wfp::classify_event<flow_layer> &event) noexcept {
      edit_state *const state = &owned_state;
      if (!state->target)
        return;

      const auto handle = event.metadata().flow_handle();
      const auto protocol =
          event.value(flow_layer::field::protocol).uint8();
      const auto direction =
          event.value(flow_layer::field::direction).uint32();
      if (!handle || !protocol || !direction ||
          *protocol != IPPROTO_TCP ||
          *direction != FWP_DIRECTION_OUTBOUND)
        return;

      try {
        auto flow = std::make_shared<edit_flow>(*state);
        std::unique_ptr<edit_flow_context> context(
            new (std::nothrow)
                edit_flow_context(std::move(flow)));
        if (context)
          (void)state->target->associate(
              *handle, std::move(context));
      } catch (...) {
      }
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto injector =
      ntl::wfp::stream_injector::try_create(
          driver.native_handle(), AF_INET);
  if (!injector)
    return injector.status();

  auto state = std::make_shared<edit_state>(std::move(*injector));
  ntl::wfp::callout_driver<> callouts(driver);

  auto target = callouts.add_stream<edit_flow_context>(
      wfp_stream_edit::stream_callout_key, state, edit_stream);
  if (!target)
    return target.status();
  state->target = *target;

  const ntl::status flow_status = callouts.add_inspection(
      wfp_stream_edit::flow_callout_key, state, begin_edit_flow);
  if (!flow_status.is_ok())
    return flow_status;

  driver.on_unload([state, callouts] {
    state->stop();
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
