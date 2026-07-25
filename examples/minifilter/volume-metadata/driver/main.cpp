#include <ntl/flt/all>

#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::wstring_view metadata_path =
    L"\\System Volume Information\\CrtSysMetadataSample.md";

struct volume_state {
  ntl::flt::volume_metadata_file metadata;

  volume_state(ntl::flt::related_objects objects,
               std::wstring &&path) noexcept
      : metadata(objects, std::move(path)) {}
};

inline constexpr ntl::flt::volume_metadata_instance_context<volume_state>
    metadata_context;

struct snapshot_state {
  ntl::flt::context_ref<volume_state, ntl::flt::context_scope::instance>
      instance;
  ntl::flt::volume_metadata_snapshot_hold hold;

  snapshot_state(
      ntl::flt::context_ref<volume_state,
                            ntl::flt::context_scope::instance>
          &&context,
      ntl::flt::volume_metadata_snapshot_hold &&token) noexcept
      : instance(std::move(context)), hold(std::move(token)) {}
};

ntl::status release_metadata(ntl::flt::related_objects objects) noexcept {
  auto state = objects.try_get(metadata_context);
  if (!state)
    return state.status();
  return (*state)->metadata.try_release_for(objects.file());
}

void reopen_metadata(ntl::flt::related_objects objects) noexcept {
  if (auto state = objects.try_get(metadata_context))
    (void)(*state)->metadata.try_reopen_for(objects.file());
}

ntl::flt::pre_result
pre_create(ntl::flt::create_callback_data data,
           ntl::flt::related_objects objects) noexcept {
  if (!objects.file().is_volume_open() ||
      !data.parameters().is_implicit_volume_lock_candidate()) {
    return ntl::flt::pre_result::success_no_callback;
  }

  const ntl::status status = release_metadata(objects);
  if (status.is_err()) {
    data.complete(status);
    return ntl::flt::pre_result::complete;
  }
  return ntl::flt::pre_result::synchronize;
}

void post_create(ntl::flt::create_callback_data data,
                 ntl::flt::related_objects objects) noexcept {
  if (data.io_status().is_err())
    reopen_metadata(objects);
}

ntl::flt::pre_result
pre_file_system_control(
    ntl::flt::file_system_control_callback_data data,
    ntl::flt::related_objects objects) noexcept {
  if (!objects.file().is_volume_open())
    return ntl::flt::pre_result::success_no_callback;

  switch (data.parameters().volume_request()) {
  case ntl::flt::volume_control_request::lock:
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
}

void post_file_system_control(
    ntl::flt::file_system_control_callback_data data,
    ntl::flt::related_objects objects) noexcept {
  switch (data.parameters().volume_request()) {
  case ntl::flt::volume_control_request::lock:
    if (data.io_status().is_err())
      reopen_metadata(objects);
    break;
  case ntl::flt::volume_control_request::unlock:
    if (data.io_status().is_ok())
      reopen_metadata(objects);
    break;
  case ntl::flt::volume_control_request::dismount:
    if (data.io_status().is_ok())
      (void)objects.filter().try_detach(objects.volume());
    else
      reopen_metadata(objects);
    break;
  case ntl::flt::volume_control_request::other:
  default:
    break;
  }
}

} // namespace

ntl::status ntl::flt::main(ntl::flt::driver &driver, std::wstring_view) {
  ntl::flt::registration callbacks;
  callbacks
      .context(metadata_context)
      .on(ntl::flt::operation::create, pre_create, post_create)
      .on(ntl::flt::operation::cleanup,
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
      .on(ntl::flt::operation::file_system_control,
          pre_file_system_control, post_file_system_control)
      .on_with_completion<snapshot_state>(
          ntl::flt::operation::device_control,
          [](ntl::flt::device_control_callback_data data,
             ntl::flt::related_objects objects,
             ntl::flt::completion_slot<snapshot_state>
                 &completion) noexcept {
            if (!data.parameters().is_snapshot_flush_and_hold())
              return ntl::flt::pre_result::success_no_callback;

            auto state = objects.try_get(metadata_context);
            if (!state) {
              data.complete(state.status());
              return ntl::flt::pre_result::complete;
            }
            auto hold = (*state)->metadata.try_hold_updates_for_snapshot();
            if (!hold) {
              data.complete(hold.status());
              return ntl::flt::pre_result::complete;
            }
            const ntl::status stored =
                completion.try_emplace(std::move(*state), std::move(*hold));
            if (stored.is_err()) {
              data.complete(stored);
              return ntl::flt::pre_result::complete;
            }
            return ntl::flt::pre_result::success_with_callback;
          },
          [](ntl::flt::device_control_callback_data,
             ntl::flt::related_objects,
             ntl::flt::completion_ref<snapshot_state>,
             ntl::flt::post_operation_flags) noexcept {})
      .on(ntl::flt::operation::pnp,
          [](ntl::flt::pnp_callback_data data,
             ntl::flt::related_objects objects) noexcept {
            switch (data.parameters().request()) {
            case ntl::flt::pnp_request::query_remove: {
              const ntl::status status = release_metadata(objects);
              if (status.is_err()) {
                data.complete(status);
                return ntl::flt::pre_result::complete;
              }
              return ntl::flt::pre_result::success_no_callback;
            }
            case ntl::flt::pnp_request::cancel_remove:
              return ntl::flt::pre_result::synchronize;
            case ntl::flt::pnp_request::surprise_removal:
              if (auto state = objects.try_get(metadata_context))
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
            if (auto state = objects.try_get(metadata_context))
              (void)(*state)->metadata.try_close();
            return ntl::flt::pre_result::success_no_callback;
          })
      .on_instance_setup(
          [](ntl::flt::related_objects objects, FLT_INSTANCE_SETUP_FLAGS,
             DEVICE_TYPE, FLT_FILESYSTEM_TYPE filesystem) noexcept {
            if (filesystem != FLT_FSTYPE_NTFS &&
                filesystem != FLT_FSTYPE_REFS) {
              return ntl::status{STATUS_FLT_DO_NOT_ATTACH};
            }

            auto state = objects.try_get_or_create(
                metadata_context, objects, std::wstring(metadata_path));
            if (!state)
              return state.status();

            ntl::flt::volume_metadata_open_options options;
            options.create_system_volume_information = true;
            return (*state)->metadata.try_open(options);
          })
      .on_instance_teardown_start(
          [](ntl::flt::related_objects objects,
             FLT_INSTANCE_TEARDOWN_FLAGS) noexcept {
            if (auto state = objects.try_get(metadata_context))
              (void)(*state)->metadata.try_close();
          });

  return driver.start(std::move(callbacks));
}
