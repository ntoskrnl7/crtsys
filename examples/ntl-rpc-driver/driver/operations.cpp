#include <ntddk.h>

#include <ntl/except>

#include "operations.hpp"

namespace crtsys_ntl_rpc_sample_server {
namespace {

constexpr std::uint32_t series_max_count = 4096;
constexpr std::uint32_t delay_max_milliseconds = 10'000;
constexpr std::uint32_t cancellation_poll_milliseconds = 10;

void delay(ntl::rpc::call_context call, std::uint32_t milliseconds) {
  if (milliseconds > delay_max_milliseconds) {
    throw ntl::exception(STATUS_INVALID_PARAMETER, "delay is too large");
  }

  std::uint32_t elapsed = 0;
  while (elapsed < milliseconds) {
    call.throw_if_cancelled();

    const auto remaining = milliseconds - elapsed;
    const auto interval_milliseconds =
        remaining < cancellation_poll_milliseconds
            ? remaining
            : cancellation_poll_milliseconds;
    LARGE_INTEGER interval{};
    interval.QuadPart =
        -static_cast<LONGLONG>(interval_milliseconds) * 10'000;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
    elapsed += interval_milliseconds;
  }

  call.throw_if_cancelled();
}

} // namespace

int add(int left, int right) noexcept {
  DbgPrint("crtsys_ntl_rpc_sample::add kernel callback: left=%d right=%d\n",
           left, right);
  return left + right;
}

ntl_rpc_sample_reply describe(const ntl_rpc_sample_request &request) noexcept {
  ntl_rpc_sample_reply reply{};
  reply.value = request.value;
  reply.doubled = request.value * 2;
  reply.biased = request.value + request.bias;
  const auto server_irql = KeGetCurrentIrql();
  reply.server_irql = static_cast<std::uint32_t>(server_irql);
  DbgPrint("crtsys_ntl_rpc_sample::describe kernel callback: "
           "value=%lu bias=%lu irql=%lu\n",
           static_cast<unsigned long>(request.value),
           static_cast<unsigned long>(request.bias),
           static_cast<unsigned long>(server_irql));
  return reply;
}

std::vector<std::uint32_t> series(std::uint32_t count) {
  if (count > series_max_count) {
    throw ntl::exception(STATUS_INVALID_PARAMETER, "series is too large");
  }

  DbgPrint("crtsys_ntl_rpc_sample::series kernel callback: count=%lu\n",
           static_cast<unsigned long>(count));
  std::vector<std::uint32_t> values;
  values.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    values.push_back(index + 1);
  }
  return values;
}

int delayed_add(ntl::rpc::call_context call, std::uint32_t milliseconds,
                int left, int right) {
  DbgPrint("crtsys_ntl_rpc_sample::delayed_add kernel callback: "
           "delay=%lu left=%d right=%d\n",
           static_cast<unsigned long>(milliseconds), left, right);
  delay(call, milliseconds);
  return left + right;
}

} // namespace crtsys_ntl_rpc_sample_server
