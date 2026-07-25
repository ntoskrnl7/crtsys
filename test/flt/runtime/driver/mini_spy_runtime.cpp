#include "mini_spy_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace crtsys_flt_runtime_test {
namespace {

class mini_spy_ring {
public:
  void initialize() noexcept {
    KeInitializeSpinLock(&lock_);
    reset();
  }

  void reset() noexcept {
    KIRQL irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&lock_, &irql);
    head_ = 0;
    count_ = 0;
    next_sequence_ = 1;
    dropped_ = 0;
    discarded_on_close_ = 0;
    closed_ = false;
    KeReleaseSpinLock(&lock_, irql);
  }

  void record(mini_spy_operation operation, mini_spy_phase phase,
              NTSTATUS status, std::uint32_t information) noexcept {
    KIRQL irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&lock_, &irql);
    if (closed_) {
      KeReleaseSpinLock(&lock_, irql);
      return;
    }

    if (count_ == records_.size()) {
      head_ = (head_ + 1) % records_.size();
      --count_;
      ++dropped_;
    }
    const std::size_t tail = (head_ + count_) % records_.size();
    records_[tail] = mini_spy_record{
        next_sequence_++, static_cast<std::uint32_t>(operation),
        static_cast<std::uint32_t>(phase), status, information};
    ++count_;
    KeReleaseSpinLock(&lock_, irql);
  }

  mini_spy_batch read() noexcept {
    mini_spy_batch batch{};
    KIRQL irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&lock_, &irql);
    batch.count = static_cast<std::uint32_t>(
        (std::min)(count_, batch.records.size()));
    for (std::size_t index = 0; index != batch.count; ++index) {
      batch.records[index] = records_[(head_ + index) % records_.size()];
    }
    head_ = (head_ + batch.count) % records_.size();
    count_ -= batch.count;
    batch.remaining = static_cast<std::uint32_t>(count_);
    batch.dropped = dropped_;
    batch.discarded_on_close = discarded_on_close_;
    batch.closed = closed_ ? 1u : 0u;
    KeReleaseSpinLock(&lock_, irql);
    return batch;
  }

  mini_spy_batch close() noexcept {
    mini_spy_batch batch{};
    KIRQL irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&lock_, &irql);
    if (!closed_) {
      discarded_on_close_ = static_cast<std::uint32_t>(count_);
      head_ = 0;
      count_ = 0;
      closed_ = true;
    }
    batch.dropped = dropped_;
    batch.discarded_on_close = discarded_on_close_;
    batch.closed = 1;
    KeReleaseSpinLock(&lock_, irql);
    return batch;
  }

private:
  KSPIN_LOCK lock_{};
  std::array<mini_spy_record, mini_spy_ring_capacity> records_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  std::uint64_t next_sequence_ = 1;
  std::uint64_t dropped_ = 0;
  std::uint32_t discarded_on_close_ = 0;
  bool closed_ = false;
};

mini_spy_ring records;

} // namespace

void initialize_mini_spy_runtime() noexcept { records.initialize(); }

void configure_mini_spy_runtime_messages(
    ntl::flt::communication_server &server) {
  server
      .on(mini_spy_reset_method, []() noexcept -> std::uint32_t {
        records.reset();
        return 1;
      })
      .on(mini_spy_read_method, []() noexcept { return records.read(); })
      .on(mini_spy_close_for_unload_method,
          []() noexcept { return records.close(); });
}

void record_mini_spy_operation(mini_spy_operation operation,
                               mini_spy_phase phase, NTSTATUS status,
                               std::uint32_t information) noexcept {
  records.record(operation, phase, status, information);
}

mini_spy_batch close_mini_spy_runtime() noexcept { return records.close(); }

} // namespace crtsys_flt_runtime_test
