#include <ntddk.h>

#include <atomic>
#include <memory>
#include <utility>

#include <ntl/driver>
#include <ntl/passive_executor>
#include <ntl/wfp/all>

#include "async_inspection_contract.hpp"

namespace {

class inspection_queue {
public:
  inspection_queue() noexcept : executor_(DelayedWorkQueue, "Wfai") {
    ExInitializeRundownProtection(&rundown_);
  }

  inspection_queue(const inspection_queue &) = delete;
  inspection_queue &operator=(const inspection_queue &) = delete;

  bool accepting() const noexcept {
    return accepting_.load(std::memory_order_acquire);
  }

  ntl::status post(ntl::wfp::pended_operation &&operation) noexcept {
    if (!accepting() || !ExAcquireRundownProtection(&rundown_))
      return STATUS_DELETE_PENDING;

    // Keep reauthorization fail-closed while the work item is being
    // admitted.  If post() cannot allocate/queue its item, destruction of
    // the moved operation may immediately trigger reauthorization.
    admissions_.fetch_add(1, std::memory_order_acq_rel);
    const ntl::status queued = executor_.post(
        [this, operation = std::move(operation)]() mutable noexcept {
          LARGE_INTEGER interval{};
          interval.QuadPart = -1000000; // 100 ms in relative 100-ns units.
          (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
          completed_.fetch_add(1, std::memory_order_relaxed);
          operation.complete();
          ExReleaseRundownProtection(&rundown_);
        });
    if (!queued.is_ok()) {
      failed_.store(true, std::memory_order_release);
      ExReleaseRundownProtection(&rundown_);
    } else {
      pended_.fetch_add(1, std::memory_order_relaxed);
    }
    admissions_.fetch_sub(1, std::memory_order_acq_rel);
    return queued;
  }

  bool may_reauthorize() const noexcept {
    return !failed_.load(std::memory_order_acquire) &&
           admissions_.load(std::memory_order_acquire) == 0;
  }

  void shutdown() noexcept {
    accepting_.store(false, std::memory_order_release);
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExWaitForRundownProtectionRelease(&rundown_);
  }

private:
  ntl::passive_executor executor_;
  EX_RUNDOWN_REF rundown_{};
  std::atomic<bool> accepting_{true};
  std::atomic<bool> failed_{false};
  std::atomic<std::uint32_t> admissions_{0};
  std::atomic<std::uint64_t> pended_{0};
  std::atomic<std::uint64_t> completed_{0};
};

template <class Layer>
ntl::wfp::terminating_decision
inspect_connect(inspection_queue &queue,
                const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto flags = event.value(Layer::field::flags).uint32();
  const auto policy = event.filter().context();
  if (!flags || (policy != wfp_async_inspection::permit_context &&
                 policy != wfp_async_inspection::block_context))
    return ntl::wfp::terminating_decision::block;

  if ((*flags & FWP_CONDITION_FLAG_IS_REAUTHORIZE) != 0) {
    if (!queue.may_reauthorize())
      return ntl::wfp::terminating_decision::block;
    return policy == wfp_async_inspection::permit_context
               ? ntl::wfp::terminating_decision::permit
               : ntl::wfp::terminating_decision::block;
  }

  if (!queue.accepting())
    return ntl::wfp::terminating_decision::block;

  auto operation = ntl::wfp::pended_operation::try_create(event.metadata());
  if (!operation)
    return ntl::wfp::terminating_decision::block;

  const ntl::status queued = queue.post(std::move(*operation));
  if (!queued.is_ok())
    return ntl::wfp::terminating_decision::block_and_absorb;

  return ntl::wfp::terminating_decision::block_and_absorb;
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto queue = std::make_shared<inspection_queue>();
  ntl::wfp::callout_driver<> callouts(driver);

  const ntl::status registered_v4 = callouts.add_terminating(
      wfp_async_inspection::callout_key_v4, queue,
      [](inspection_queue &owned_queue,
         const ntl::wfp::classify_event<
             wfp_async_inspection::layer_v4> &event) noexcept {
        return inspect_connect(owned_queue, event);
      });
  if (!registered_v4.is_ok())
    return registered_v4;

  const ntl::status registered_v6 = callouts.add_terminating(
      wfp_async_inspection::callout_key_v6, queue,
      [](inspection_queue &owned_queue,
         const ntl::wfp::classify_event<
             wfp_async_inspection::layer_v6> &event) noexcept {
        return inspect_connect(owned_queue, event);
      });
  if (!registered_v6.is_ok())
    return registered_v6;

  driver.on_unload([queue, callouts] {
    queue->shutdown();
    const ntl::status result = callouts.close();
    NT_ASSERT(result.is_ok());
  });
  return ntl::status::ok();
}
