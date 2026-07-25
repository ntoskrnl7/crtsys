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

void configure(ntl::device<control_state> &device) {
  auto *const state = &device.extension();
  active_state = state;

  device
      .on_create([state](ntl::irp &request) {
        bool expected = false;
        if (!state->open.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
          request.fail(STATUS_SHARING_VIOLATION);
          return;
        }
        request.succeed(FILE_OPENED);
      })
      .on_cleanup([](ntl::irp &request) { request.succeed(); })
      .on_close([state](ntl::irp &request) {
        state->open.store(false, std::memory_order_release);
        request.succeed();
      })
      .on_device_control(
          [state](const ntl::device_control::code &code,
                  const ntl::device_control::in_buffer &input,
                  ntl::device_control::out_buffer &output) {
            if (!ntl::is_ioctl<ping_ioctl_type>(code))
              throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                   "unknown control-device IOCTL");

            const auto *request =
                ntl::ioctl_input_as<ping_ioctl_type>(input);
            if (!request)
              throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                   "ping input is too small");

            ping_reply reply{};
            reply.value = request->value + 1;
            reply.sequence =
                state->sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            reply.unload_vetoes =
                state->unload_vetoes.load(std::memory_order_acquire);
            if (!ntl::ioctl_write_output<ping_ioctl_type>(output, reply))
              throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                   "ping output is too small");
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
