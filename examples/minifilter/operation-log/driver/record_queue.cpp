#include "record_queue.hpp"

#include <algorithm>

namespace crtsys_minifilter_operation_log_sample {

record_queue operation_records;

void record_queue::initialize() noexcept {
  KeInitializeSpinLock(&lock_);
  reset();
}

void record_queue::reset() noexcept {
  KIRQL old_irql = PASSIVE_LEVEL;
  KeAcquireSpinLock(&lock_, &old_irql);
  head_ = 0;
  count_ = 0;
  next_sequence_ = 1;
  dropped_ = 0;
  closed_ = false;
  KeReleaseSpinLock(&lock_, old_irql);
}

void record_queue::close() noexcept {
  KIRQL old_irql = PASSIVE_LEVEL;
  KeAcquireSpinLock(&lock_, &old_irql);
  head_ = 0;
  count_ = 0;
  closed_ = true;
  KeReleaseSpinLock(&lock_, old_irql);
}

void record_queue::push(operation operation_id, phase phase_id,
                        NTSTATUS status,
                        std::uint32_t information) noexcept {
  KIRQL old_irql = PASSIVE_LEVEL;
  KeAcquireSpinLock(&lock_, &old_irql);

  if (closed_) {
    KeReleaseSpinLock(&lock_, old_irql);
    return;
  }

  if (count_ == records_.size()) {
    head_ = (head_ + 1) % records_.size();
    --count_;
    ++dropped_;
  }

  const std::size_t tail = (head_ + count_) % records_.size();
  records_[tail] = record{next_sequence_++, operation_id, phase_id, status,
                          information};
  ++count_;
  KeReleaseSpinLock(&lock_, old_irql);
}

record_batch record_queue::read() noexcept {
  record_batch batch{};
  KIRQL old_irql = PASSIVE_LEVEL;
  KeAcquireSpinLock(&lock_, &old_irql);

  batch.count = static_cast<std::uint32_t>(
      (std::min)(count_, batch.records.size()));
  for (std::size_t index = 0; index != batch.count; ++index)
    batch.records[index] = records_[(head_ + index) % records_.size()];

  head_ = (head_ + batch.count) % records_.size();
  count_ -= batch.count;
  batch.remaining = static_cast<std::uint32_t>(count_);
  batch.dropped = dropped_;

  KeReleaseSpinLock(&lock_, old_irql);
  return batch;
}

} // namespace crtsys_minifilter_operation_log_sample
