#include <ntl/flt/all>

#include "../metadata_shared/metadata_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace {
using namespace crtsys_flt_metadata_runtime;

std::atomic<std::int32_t> last_open_status{STATUS_SUCCESS};
std::atomic<std::int32_t> last_transition_status{STATUS_SUCCESS};
std::atomic<std::uint32_t> instances_opened{0};
std::atomic<std::uint32_t> instance_teardowns{0};
std::atomic<std::uint32_t> instances_closed{0};
std::atomic<std::uint32_t> releases{0};
std::atomic<std::uint32_t> reopen_attempts{0};
std::atomic<std::uint32_t> reopens{0};
std::atomic<std::uint32_t> reopen_noops{0};
std::atomic<std::uint32_t> implicit_lock_pre{0};
std::atomic<std::uint32_t> implicit_lock_succeeded{0};
std::atomic<std::uint32_t> explicit_lock_pre{0};
std::atomic<std::uint32_t> explicit_lock_succeeded{0};
std::atomic<std::uint32_t> explicit_lock_failed{0};
std::atomic<std::uint32_t> unlock_succeeded{0};
std::atomic<std::uint32_t> dismount_succeeded{0};
std::atomic<std::uint32_t> dismount_failed{0};
std::atomic<std::uint32_t> snapshot_pre{0};
std::atomic<std::uint32_t> snapshot_post{0};
std::atomic<std::uint32_t> snapshot_update_rejected{0};
std::atomic<std::uint32_t> query_remove{0};
std::atomic<std::uint32_t> cancel_remove{0};
std::atomic<std::uint32_t> surprise_remove{0};

struct metadata_instance_state {
  ntl::flt::volume_metadata_file metadata;

  metadata_instance_state(ntl::flt::related_objects objects,
                          std::wstring &&path) noexcept
      : metadata(objects, std::move(path)) {}
};

inline constexpr
    ntl::flt::volume_metadata_instance_context<metadata_instance_state>
        metadata_instance_context;

struct snapshot_completion {
  ntl::flt::context_ref<metadata_instance_state,
                        ntl::flt::context_scope::instance>
      instance;
  ntl::flt::volume_metadata_snapshot_hold hold;

  snapshot_completion(
      ntl::flt::context_ref<metadata_instance_state,
                            ntl::flt::context_scope::instance>
          &&state,
      ntl::flt::volume_metadata_snapshot_hold &&snapshot) noexcept
      : instance(std::move(state)), hold(std::move(snapshot)) {}
};

observations capture_observations_impl() noexcept {
  observations result;
  result.last_open_status =
      last_open_status.load(std::memory_order_acquire);
  result.last_transition_status =
      last_transition_status.load(std::memory_order_acquire);
  result.instances_opened = instances_opened.load(std::memory_order_acquire);
  result.instance_teardowns =
      instance_teardowns.load(std::memory_order_acquire);
  result.instances_closed = instances_closed.load(std::memory_order_acquire);
  result.releases = releases.load(std::memory_order_acquire);
  result.reopen_attempts =
      reopen_attempts.load(std::memory_order_acquire);
  result.reopens = reopens.load(std::memory_order_acquire);
  result.reopen_noops = reopen_noops.load(std::memory_order_acquire);
  result.implicit_lock_pre =
      implicit_lock_pre.load(std::memory_order_acquire);
  result.implicit_lock_succeeded =
      implicit_lock_succeeded.load(std::memory_order_acquire);
  result.explicit_lock_pre =
      explicit_lock_pre.load(std::memory_order_acquire);
  result.explicit_lock_succeeded =
      explicit_lock_succeeded.load(std::memory_order_acquire);
  result.explicit_lock_failed =
      explicit_lock_failed.load(std::memory_order_acquire);
  result.unlock_succeeded =
      unlock_succeeded.load(std::memory_order_acquire);
  result.dismount_succeeded =
      dismount_succeeded.load(std::memory_order_acquire);
  result.dismount_failed =
      dismount_failed.load(std::memory_order_acquire);
  result.snapshot_pre = snapshot_pre.load(std::memory_order_acquire);
  result.snapshot_post = snapshot_post.load(std::memory_order_acquire);
  result.snapshot_update_rejected =
      snapshot_update_rejected.load(std::memory_order_acquire);
  result.query_remove = query_remove.load(std::memory_order_acquire);
  result.cancel_remove = cancel_remove.load(std::memory_order_acquire);
  result.surprise_remove = surprise_remove.load(std::memory_order_acquire);
  return result;
}

ntl::status release_metadata(ntl::flt::related_objects objects) noexcept {
  auto state = objects.try_get(metadata_instance_context);
  if (!state)
    return state.status();
  const ntl::status status = (*state)->metadata.try_release_for(objects.file());
  last_transition_status.store(static_cast<NTSTATUS>(status),
                               std::memory_order_release);
  if (status.is_ok())
    releases.fetch_add(1, std::memory_order_relaxed);
  return status;
}

void reopen_metadata(ntl::flt::related_objects objects) noexcept {
  reopen_attempts.fetch_add(1, std::memory_order_relaxed);
  auto state = objects.try_get(metadata_instance_context);
  if (!state) {
    last_transition_status.store(
        static_cast<NTSTATUS>(state.status()), std::memory_order_release);
    return;
  }
  const std::uint64_t before = (*state)->metadata.open_generation();
  const ntl::status status = (*state)->metadata.try_reopen_for(objects.file());
  last_transition_status.store(static_cast<NTSTATUS>(status),
                               std::memory_order_release);
  if (status.is_ok() &&
      (*state)->metadata.open_generation() > before) {
    reopens.fetch_add(1, std::memory_order_relaxed);
  } else if (status.is_ok()) {
    reopen_noops.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace

crtsys_flt_metadata_runtime::observations
crtsys_flt_metadata_runtime::capture_observations() noexcept {
  return capture_observations_impl();
}

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  using namespace crtsys_flt_metadata_runtime;

  auto messages = make_server();
  auto port_status =
      driver.add_communication_port(port_name, std::move(messages));
  if (port_status.is_err())
    return port_status;

  ntl::flt::registration callbacks;
  callbacks
      .context(metadata_instance_context)
      .on_with_completion<snapshot_completion>(
          ntl::flt::operation::device_control,
          [](ntl::flt::device_control_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_slot<snapshot_completion>
                 &completion) noexcept {
            if (!data.parameters().is_snapshot_flush_and_hold())
              return ntl::flt::pre_result::success_no_callback;

            auto state = objects.try_get(metadata_instance_context);
            if (!state) {
              data.complete(state.status());
              return ntl::flt::pre_result::complete;
            }
            auto hold = (*state)->metadata.try_hold_updates_for_snapshot();
            if (!hold) {
              data.complete(hold.status());
              return ntl::flt::pre_result::complete;
            }
            snapshot_pre.fetch_add(1, std::memory_order_relaxed);

            auto forbidden_update = (*state)->metadata.try_begin_update();
            if (!forbidden_update &&
                static_cast<NTSTATUS>(forbidden_update.status()) ==
                    STATUS_DEVICE_BUSY) {
              snapshot_update_rejected.fetch_add(
                  1, std::memory_order_relaxed);
            }

            if (completion
                    .try_emplace(std::move(*state), std::move(*hold))
                    .is_err()) {
              data.complete(STATUS_INSUFFICIENT_RESOURCES);
              return ntl::flt::pre_result::complete;
            }
            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::device_control_callback_data,
             ntl::flt::related_objects,
             ntl::flt::completion_ref<snapshot_completion> completion,
             ntl::flt::post_operation_flags) noexcept {
            if (completion)
              snapshot_post.fetch_add(1, std::memory_order_relaxed);
          })
      .on(
          ntl::flt::operation::create,
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            if (!objects.file().is_volume_open() ||
                !data.parameters().is_implicit_volume_lock_candidate())
              return ntl::flt::pre_result::success_no_callback;

            implicit_lock_pre.fetch_add(1, std::memory_order_relaxed);
            const ntl::status status = release_metadata(objects);
            if (status.is_err()) {
              data.complete(status);
              return ntl::flt::pre_result::complete;
            }
            return ntl::flt::pre_result::synchronize;
          },
          [](ntl::flt::create_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            if (data.io_status().is_ok())
              implicit_lock_succeeded.fetch_add(1,
                                                std::memory_order_relaxed);
            else
              reopen_metadata(objects);
          })
      .on(
          ntl::flt::operation::cleanup,
          [](ntl::flt::cleanup_callback_data,
             ntl::flt::related_objects objects) noexcept {
            return objects.file().is_volume_open()
                       ? ntl::flt::pre_result::synchronize
                       : ntl::flt::pre_result::success_no_callback;
          },
          [](ntl::flt::cleanup_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            if (data.io_status().is_ok())
              reopen_metadata(objects);
          })
      .on(
          ntl::flt::operation::file_system_control,
          [](ntl::flt::file_system_control_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            if (!objects.file().is_volume_open())
              return ntl::flt::pre_result::success_no_callback;

            switch (data.parameters().volume_request()) {
            case ntl::flt::volume_control_request::lock:
              explicit_lock_pre.fetch_add(1, std::memory_order_relaxed);
              [[fallthrough]];
            case ntl::flt::volume_control_request::dismount: {
              const ntl::status status = release_metadata(objects);
              if (status.is_err()) {
                data.complete(status);
                return ntl::flt::pre_result::complete;
              }
              return ntl::flt::pre_result::synchronize;
            }
            case ntl::flt::volume_control_request::unlock:
              return ntl::flt::pre_result::synchronize;
            case ntl::flt::volume_control_request::other:
            default:
              return ntl::flt::pre_result::success_no_callback;
            }
          },
          [](ntl::flt::file_system_control_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            switch (data.parameters().volume_request()) {
            case ntl::flt::volume_control_request::lock:
              if (data.io_status().is_ok()) {
                explicit_lock_succeeded.fetch_add(
                    1, std::memory_order_relaxed);
              } else {
                explicit_lock_failed.fetch_add(1,
                                               std::memory_order_relaxed);
                reopen_metadata(objects);
              }
              break;
            case ntl::flt::volume_control_request::unlock:
              if (data.io_status().is_ok()) {
                unlock_succeeded.fetch_add(1,
                                           std::memory_order_relaxed);
                reopen_metadata(objects);
              }
              break;
            case ntl::flt::volume_control_request::dismount:
              if (data.io_status().is_ok()) {
                dismount_succeeded.fetch_add(1,
                                             std::memory_order_relaxed);
                (void)objects.filter().try_detach(objects.volume());
              } else {
                dismount_failed.fetch_add(1, std::memory_order_relaxed);
                reopen_metadata(objects);
              }
              break;
            case ntl::flt::volume_control_request::other:
            default:
              break;
            }
          })
      .on(
          ntl::flt::operation::pnp,
          [](ntl::flt::pnp_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            switch (data.parameters().request()) {
            case ntl::flt::pnp_request::query_remove: {
              query_remove.fetch_add(1, std::memory_order_relaxed);
              const ntl::status status = release_metadata(objects);
              if (status.is_err()) {
                data.complete(status);
                return ntl::flt::pre_result::complete;
              }
              return ntl::flt::pre_result::success_no_callback;
            }
            case ntl::flt::pnp_request::cancel_remove:
              cancel_remove.fetch_add(1, std::memory_order_relaxed);
              return ntl::flt::pre_result::synchronize;
            case ntl::flt::pnp_request::surprise_removal:
              surprise_remove.fetch_add(1, std::memory_order_relaxed);
              if (auto state = objects.try_get(metadata_instance_context))
                (void)(*state)->metadata.try_close();
              (void)objects.filter().try_detach(objects.volume());
              return ntl::flt::pre_result::success_no_callback;
            case ntl::flt::pnp_request::other:
            default:
              return ntl::flt::pre_result::success_no_callback;
            }
          },
          [](ntl::flt::pnp_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            if (data.parameters().request() ==
                    ntl::flt::pnp_request::cancel_remove &&
                data.io_status().is_ok()) {
              reopen_metadata(objects);
            }
          })
      .on(ntl::flt::operation::shutdown,
          [](ntl::flt::shutdown_callback_data,
             ntl::flt::related_objects objects, void *&) noexcept {
            if (auto state = objects.try_get(metadata_instance_context))
              (void)(*state)->metadata.try_close();
            return ntl::flt::pre_result::success_no_callback;
          })
      .on_instance_setup(
          [](ntl::flt::related_objects objects, FLT_INSTANCE_SETUP_FLAGS,
             DEVICE_TYPE, FLT_FILESYSTEM_TYPE filesystem) noexcept {
            if (filesystem != FLT_FSTYPE_NTFS &&
                filesystem != FLT_FSTYPE_REFS)
              return ntl::status{STATUS_FLT_DO_NOT_ATTACH};

            auto state = objects.try_get_or_create(
                metadata_instance_context, objects,
                std::wstring(metadata_relative_path));
            if (!state)
              return state.status();

            ntl::flt::volume_metadata_open_options options;
            options.create_system_volume_information = true;
            const ntl::status status = (*state)->metadata.try_open(options);
            last_open_status.store(static_cast<NTSTATUS>(status),
                                   std::memory_order_release);
            if (status.is_ok())
              instances_opened.fetch_add(1, std::memory_order_relaxed);
            return status;
          })
      .on_instance_teardown_start(
          [](ntl::flt::related_objects objects,
             FLT_INSTANCE_TEARDOWN_FLAGS) noexcept {
            instance_teardowns.fetch_add(1, std::memory_order_relaxed);
            if (auto state = objects.try_get(metadata_instance_context)) {
              const bool was_open = (*state)->metadata.is_open();
              (void)(*state)->metadata.try_close();
              if (was_open)
                instances_closed.fetch_add(1,
                                           std::memory_order_relaxed);
            }
          });

  return driver.start(std::move(callbacks));
}
