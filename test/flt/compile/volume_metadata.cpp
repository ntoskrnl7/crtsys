#include <ntl/flt/callback>
#include <ntl/flt/parameters>
#include <ntl/flt/registration>
#include <ntl/flt/volume_metadata>

#include <string>
#include <type_traits>
#include <utility>

namespace {

struct metadata_state {
  ntl::flt::volume_metadata_file file;

  metadata_state(ntl::flt::related_objects objects,
                 std::wstring &&path) noexcept
      : file(objects, std::move(path)) {}
};

inline constexpr ntl::flt::volume_metadata_instance_context<metadata_state>
    metadata_state_context;

static_assert(
    std::is_same_v<
        ntl::flt::operation_parameters_t<ntl::flt::operation_id::pnp>,
        ntl::flt::pnp_parameters>);
static_assert(metadata_state_context.pool() ==
              ntl::flt::context_pool::nonpaged_nx);
static_assert(
    !std::is_constructible_v<
        ntl::flt::volume_metadata_instance_context<metadata_state>,
        ntl::flt::context_pool>);
static_assert(!std::is_copy_constructible_v<ntl::flt::volume_metadata_update>);
static_assert(
    !std::is_copy_constructible_v<ntl::flt::volume_metadata_snapshot_hold>);

void compile_volume_metadata_registration() {
  ntl::flt::registration callbacks;
  callbacks
      .context(metadata_state_context)
      .on(ntl::flt::operation::file_system_control,
          [](ntl::flt::file_system_control_callback_data data,
             ntl::flt::related_objects, void *&) noexcept {
            (void)data.parameters().volume_request();
            return ntl::flt::pre_result::success_no_callback;
          })
      .on(ntl::flt::operation::device_control,
          [](ntl::flt::device_control_callback_data data,
             ntl::flt::related_objects, void *&) noexcept {
            (void)data.parameters().is_snapshot_flush_and_hold();
            return ntl::flt::pre_result::success_no_callback;
          })
      .on(ntl::flt::operation::pnp,
          [](ntl::flt::pnp_callback_data data, ntl::flt::related_objects,
             void *&) noexcept {
            (void)data.parameters().request();
            return ntl::flt::pre_result::success_no_callback;
          });
}

} // namespace
