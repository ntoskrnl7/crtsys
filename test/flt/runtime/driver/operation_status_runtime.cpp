#include "operation_status_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <atomic>
#include <cstdint>

namespace crtsys_flt_runtime_test {
namespace {

std::atomic<NTSTATUS> last_request_status{STATUS_NOT_SUPPORTED};
std::atomic<NTSTATUS> last_operation_status{STATUS_NOT_SUPPORTED};
std::atomic<std::uint32_t> matched_operations{0};
std::atomic<std::uint32_t> requests_accepted{0};
std::atomic<std::uint32_t> callbacks_seen{0};
std::atomic<std::uint32_t> pending_statuses{0};
std::atomic<std::uint32_t> snapshots_valid{0};
std::atomic<std::uint32_t> states_created{0};
std::atomic<std::uint32_t> states_destroyed{0};
std::atomic<long> armed{0};

struct directory_status_state {
  directory_status_state(UCHAR expected_minor, ULONG expected_length) noexcept
      : expected_minor(expected_minor), expected_length(expected_length) {
    states_created.fetch_add(1, std::memory_order_release);
  }

  ~directory_status_state() noexcept {
    states_destroyed.fetch_add(1, std::memory_order_release);
  }

  UCHAR expected_minor;
  ULONG expected_length;
};

void observe_directory_status(ntl::flt::operation_status_snapshot<
                                  ntl::flt::operation_id::directory_control>
                                  snapshot,
                              ntl::flt::related_objects objects,
                              ntl::status operation_status,
                              directory_status_state &state) noexcept {
  callbacks_seen.fetch_add(1, std::memory_order_release);
  last_operation_status.store(operation_status, std::memory_order_release);
  if (operation_status == STATUS_PENDING)
    pending_statuses.fetch_add(1, std::memory_order_release);

  const auto &parameters = snapshot.parameters();
  if (snapshot && objects.instance() && objects.file() &&
      snapshot.major_function() == ntl::flt::operation_id::directory_control &&
      snapshot.minor_function() == state.expected_minor &&
      parameters.is_notify() && parameters.length() == state.expected_length) {
    snapshots_valid.fetch_add(1, std::memory_order_release);
  }
}

ntl::flt::pre_result
request_directory_status(ntl::flt::directory_control_callback_data data,
                         ntl::flt::related_objects, void *&) noexcept {
  const auto parameters = data.parameters();
  if (!data.is_irp_operation() || !parameters.is_notify() ||
      armed.exchange(0, std::memory_order_acq_rel) == 0) {
    return ntl::flt::pre_result::success_no_callback;
  }

  matched_operations.fetch_add(1, std::memory_order_release);
  const ntl::status status = data.try_request_operation_status(
      &observe_directory_status, data.minor_function(), parameters.length());
  last_request_status.store(status, std::memory_order_release);
  if (status.is_ok())
    requests_accepted.fetch_add(1, std::memory_order_release);
  return ntl::flt::pre_result::success_no_callback;
}

} // namespace

void configure_operation_status_runtime_messages(
    ntl::flt::communication_server &messages) {
  messages.on(operation_status_observations_method, []() noexcept {
    operation_status_observations result;
    result.last_request_status =
        last_request_status.load(std::memory_order_acquire);
    result.last_operation_status =
        last_operation_status.load(std::memory_order_acquire);
    result.matched_operations =
        matched_operations.load(std::memory_order_acquire);
    result.requests_accepted =
        requests_accepted.load(std::memory_order_acquire);
    result.callbacks = callbacks_seen.load(std::memory_order_acquire);
    result.pending_statuses = pending_statuses.load(std::memory_order_acquire);
    result.snapshots_valid = snapshots_valid.load(std::memory_order_acquire);
    result.states_created = states_created.load(std::memory_order_acquire);
    result.states_destroyed = states_destroyed.load(std::memory_order_acquire);
    return result;
  });
  messages.on(arm_operation_status_method, []() noexcept {
    armed.store(1, std::memory_order_release);
    return std::uint32_t{1};
  });
}

void configure_operation_status_runtime_registration(
    ntl::flt::registration &callbacks) {
  callbacks.on(ntl::flt::operation::directory_control,
               &request_directory_status);
}

} // namespace crtsys_flt_runtime_test
