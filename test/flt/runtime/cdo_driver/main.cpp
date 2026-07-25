#include <ntl/flt/all>

#include "../cdo_shared/cdo_runtime.hpp"

#include <atomic>
#include <cstdint>

namespace {
using namespace crtsys_flt_cdo_runtime;
using ping_ioctl_type = ntl::ioctl_from_contract<ping_ioctl_contract>;

struct cdo_extension {
  std::atomic<bool> open_reference{false};
  std::atomic<bool> open_handle{false};
  std::atomic<std::uint32_t> create_count{0};
  std::atomic<std::uint32_t> cleanup_count{0};
  std::atomic<std::uint32_t> close_count{0};
  std::atomic<std::uint32_t> ioctl_count{0};
  std::atomic<std::uint32_t> unload_veto_count{0};
  std::atomic<std::uint32_t> sequence{0};
};

cdo_extension *active_extension = nullptr;

ntl::status configure_control_device(
    ntl::device<cdo_extension> &device) noexcept {
  auto *const state = &device.extension();
  active_extension = state;

  device
      .on_create([state](ntl::irp &request) {
        bool expected = false;
        if (!state->open_reference.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
          request.fail(STATUS_DEVICE_ALREADY_ATTACHED);
          return;
        }

        state->open_handle.store(true, std::memory_order_release);
        state->create_count.fetch_add(1, std::memory_order_relaxed);
        request.succeed(FILE_OPENED);
      })
      .on_cleanup([state](ntl::irp &request) {
        if (state->open_handle.exchange(false,
                                        std::memory_order_acq_rel)) {
          state->cleanup_count.fetch_add(1, std::memory_order_relaxed);
        }
        request.succeed();
      })
      .on_close([state](ntl::irp &request) {
        state->open_handle.store(false, std::memory_order_release);
        if (state->open_reference.exchange(false,
                                           std::memory_order_acq_rel)) {
          state->close_count.fetch_add(1, std::memory_order_relaxed);
        }
        request.succeed();
      })
      .on_device_control(
          [state](const ntl::device_control::code &code,
                  const ntl::device_control::in_buffer &input,
                  ntl::device_control::out_buffer &output) {
            if (!ntl::is_ioctl<ping_ioctl_type>(code))
              throw ntl::exception(STATUS_INVALID_DEVICE_REQUEST,
                                   "unknown CDO runtime IOCTL");
            if (!state->open_handle.load(std::memory_order_acquire))
              throw ntl::exception(STATUS_FILE_CLOSED,
                                   "CDO handle was cleaned up");

            const auto *request =
                ntl::ioctl_input_as<ping_ioctl_type>(input);
            if (!request)
              throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                   "CDO ping input is too small");

            ping_reply reply{};
            reply.value = request->value + 1;
            reply.sequence =
                state->sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            reply.create_count =
                state->create_count.load(std::memory_order_acquire);
            reply.ioctl_count =
                state->ioctl_count.fetch_add(1,
                                             std::memory_order_acq_rel) +
                1;
            reply.unload_veto_count =
                state->unload_veto_count.load(std::memory_order_acquire);
            if (!ntl::ioctl_write_output<ping_ioctl_type>(output, reply))
              throw ntl::exception(STATUS_BUFFER_TOO_SMALL,
                                   "CDO ping output is too small");
          });

  return STATUS_SUCCESS;
}

ntl::status on_unload(ntl::flt::unload_flags flags) noexcept {
  if (!flags.mandatory() && active_extension &&
      active_extension->open_reference.load(std::memory_order_acquire)) {
    active_extension->unload_veto_count.fetch_add(
        1, std::memory_order_relaxed);
    return STATUS_FLT_DO_NOT_DETACH;
  }
  return STATUS_SUCCESS;
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  auto options = ntl::device_options()
                     .name(crtsys_flt_cdo_runtime::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false);
  const ntl::status cdo_status =
      driver.add_control_device<cdo_extension>(
          std::move(options), configure_control_device);
  if (cdo_status.is_err())
    return cdo_status;

  ntl::flt::registration callbacks;
  callbacks.on_unload(on_unload);
  return driver.start(std::move(callbacks));
}
