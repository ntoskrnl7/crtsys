#pragma once

#include <ntl/flt/all>

#include "operation_log_sample.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace crtsys_minifilter_operation_log_sample {

class record_queue {
public:
  void initialize() noexcept;
  void reset() noexcept;
  void close() noexcept;

  void push(operation operation_id, phase phase_id, NTSTATUS status,
            std::uint32_t information = 0) noexcept;
  record_batch read() noexcept;

private:
  KSPIN_LOCK lock_{};
  std::array<record, queue_capacity> records_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  std::uint64_t next_sequence_ = 1;
  std::uint64_t dropped_ = 0;
  bool closed_ = false;
};

extern record_queue operation_records;

} // namespace crtsys_minifilter_operation_log_sample
