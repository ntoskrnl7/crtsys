#include <ntl/flt/all>

#include "../shared/control_device_sample.hpp"

#include <atomic>
#include <cstdint>

namespace {
using namespace crtsys_minifilter_control_device_sample;
using ping_ioctl_type = ntl::ioctl_from_contract<ping_contract>;

struct control_state {
  std::atomic<bool> open{false};
  std::atomic<std::uint32_t> sequence{0};
  std::atomic<std::uint32_t> unload_vetoes{0};
};

control_state *active_state = nullptr;

ntl::status configure(ntl::device_endpoint<control_state> &endpoint) noexcept {
  ntl::status result = endpoint.on_create(
      [](control_state &state, ntl::irp &request) noexcept {
        active_state = &state;
        bool expected = false;
        if (!state.open.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
          request.fail(STATUS_SHARING_VIOLATION);
          return;
        }
        request.succeed(FILE_OPENED);
      });
  if (!result.is_ok())
    return result;

  result = endpoint.on_cleanup(
      [](control_state &, ntl::irp &request) noexcept { request.succeed(); });
  if (!result.is_ok())
    return result;

  result = endpoint.on_close(
      [](control_state &state, ntl::irp &request) noexcept {
        state.open.store(false, std::memory_order_release);
        request.succeed();
      });
  if (!result.is_ok())
    return result;

  return endpoint.on_ioctl<ping_ioctl_type>(
      [](control_state &state, const ping_request &request,
         ping_reply &reply) noexcept -> ntl::status {
        reply.value = request.value + 1;
        reply.sequence =
            state.sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        reply.unload_vetoes =
            state.unload_vetoes.load(std::memory_order_acquire);
        return ntl::status::ok();
      });
}

ntl::status unload(ntl::flt::unload_flags flags) noexcept {
  if (!flags.mandatory() && active_state &&
      active_state->open.load(std::memory_order_acquire)) {
    active_state->unload_vetoes.fetch_add(1, std::memory_order_relaxed);
    return STATUS_FLT_DO_NOT_DETACH;
  }
  return STATUS_SUCCESS;
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  auto device = ntl::device_options()
                    .name(device_name)
                    .type(FILE_DEVICE_UNKNOWN)
                    .exclusive(false);

  const ntl::status added =
      driver.add_control_device<control_state>(std::move(device), configure);
  if (added.is_err())
    return added;

  ntl::flt::registration callbacks;
  callbacks.on_unload(unload);
  return driver.start(std::move(callbacks));
}
