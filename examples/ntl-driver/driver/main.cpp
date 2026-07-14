#include <ntddk.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <string>
#include <vector>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/passive_executor>
#include <ntl/pool_allocator>
#include <ntl/registry>
#include <ntl/remove_lock>
#include <ntl/result>
#include <ntl/status>

#include "ntl_sample_ioctl.hpp"

namespace {

using ping_ioctl = ntl::ioctl_from_contract<ntl_sample::ping_ioctl_contract>;

struct sample_extension {
  std::uint32_t create_count = 0;
  std::uint32_t close_count = 0;
};

class sample_state {
public:
  explicit sample_state(std::uint32_t flags) noexcept : flags_(flags) {}

  std::uint32_t flags() const noexcept { return flags_; }

  std::uint32_t next_sequence() noexcept {
    return sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ntl::remove_lock &remove_lock() noexcept { return remove_lock_; }
  ntl::passive_executor &executor() noexcept { return executor_; }
  ntl::pmr::pool_resource &scratch_pool() noexcept { return scratch_pool_; }

private:
  std::uint32_t flags_ = 0;
  std::atomic<std::uint32_t> sequence_{0};
  ntl::remove_lock remove_lock_{ntl::pool_tag("NSrm")};
  ntl::passive_executor executor_{DelayedWorkQueue, "NSpw"};
  ntl::pmr::pool_resource scratch_pool_{ntl::pool_kind::nonpaged,
                                        ntl::pool_option::none, "NSpm"};
};

ntl::result<std::uint32_t> read_configured_flags(
    const std::wstring &registry_path) {
  auto config = ntl::driver_config::open(registry_path);
  if (!config) {
    const auto status = static_cast<NTSTATUS>(config.status());
    if (status == STATUS_OBJECT_NAME_NOT_FOUND ||
        status == STATUS_OBJECT_PATH_NOT_FOUND) {
      return ntl::ok(std::uint32_t{0});
    }

    return ntl::unexpected(config.status());
  }

  return ntl::ok(config->dword_or(L"Flags", 0));
}

std::uint32_t compute_checksum(sample_state &state,
                               const ntl_sample::ping_request &request,
                               std::uint32_t sequence) {
  std::uint32_t checksum = 0;

  auto work = state.executor().make_work_item([&] {
    std::pmr::vector<std::uint32_t> values{&state.scratch_pool()};
    values.push_back(request.value);
    values.push_back(sequence);
    values.push_back(state.flags());
    checksum = std::accumulate(values.begin(), values.end(), std::uint32_t{0});
  });

  const auto status = state.executor().queue_and_wait(work);
  if (!status.is_ok())
    throw ntl::exception(status, "PASSIVE_LEVEL sample work failed.");

  return checksum;
}

void handle_ping(sample_state &state,
                 const ntl::device_control::in_buffer &in,
                 ntl::device_control::out_buffer &out) {
  auto guard = state.remove_lock().acquire(&state);
  if (!guard)
    throw ntl::exception(guard.status(), "device is stopping.");

  const auto *request = ntl::ioctl_input_as<ping_ioctl>(in);
  if (!request)
    throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                         "NTL sample ping input is too small.");

  const auto sequence = state.next_sequence();
  ntl_sample::ping_reply reply{};
  reply.value = request->value + 1;
  reply.sequence = sequence;
  reply.configured_flags = state.flags();
  reply.checksum = compute_checksum(state, *request, sequence);

  if (!ntl::ioctl_write_output<ping_ioctl>(out, reply))
    throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                         "NTL sample ping output is too small.");
}

} // namespace

ntl::status ntl::main(ntl::driver &driver,
                      const std::wstring &registry_path) {
  auto configured_flags = read_configured_flags(registry_path);
  if (!configured_flags)
    return configured_flags.status();

  auto state = std::make_shared<sample_state>(*configured_flags);

  auto options = ntl::device_options()
                     .name(ntl_sample::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false);

  auto endpoint_result =
      ntl::try_create_device_endpoint<sample_extension>(driver, options);
  if (!endpoint_result)
    return endpoint_result.status();

  auto endpoint = std::make_shared<ntl::device_endpoint<sample_extension>>(
      std::move(*endpoint_result));
  auto device = endpoint->device();
  if (!device)
    return STATUS_INVALID_DEVICE_STATE;

  std::weak_ptr<ntl::device<sample_extension>> weak_device = device;

  device->on_create([weak_device](ntl::irp &request) {
    if (auto device = weak_device.lock())
      ++device->extension().create_count;
    request.succeed();
  });

  device->on_close([weak_device](ntl::irp &request) {
    if (auto device = weak_device.lock())
      ++device->extension().close_count;
    request.succeed();
  });

  device->on_device_control(
      [state](const ntl::device_control::code &code,
              const ntl::device_control::in_buffer &in,
              ntl::device_control::out_buffer &out) {
        if (ntl::is_ioctl<ping_ioctl>(code)) {
          handle_ping(*state, in, out);
          return;
        }

        out.clear();
        throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                             "unknown NTL sample IOCTL.");
      });

  driver.on_unload([endpoint, state]() mutable {
    endpoint->link().reset();
    state->remove_lock().release_and_wait(state.get());
    endpoint->reset();
  });

  return ntl::status::ok();
}
