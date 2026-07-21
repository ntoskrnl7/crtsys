/**
 * @file context.cpp
 * @author jungkwang.lee (ntoskrnl7@gmail.com)
 * @brief Compile-time coverage for typed NTL minifilter contexts.
 *
 * @copyright Copyright (c) 2022 NT Template Library Authors.
 */

#include <ntl/flt/all>

#include <type_traits>
#include <utility>

namespace ntl_flt_context_compile_test {

struct file_state {
  explicit file_state(ULONG initial) noexcept : value(initial) {}
  ~file_state() noexcept = default;

  ULONG value;
};

struct stream_state {
  stream_state() noexcept = default;
  ~stream_state() noexcept = default;
};

inline constexpr ntl::flt::file_context<file_state> file_state_context{};
inline constexpr ntl::flt::stream_context<stream_state> stream_state_context{};
inline constexpr ntl::flt::volume_context<stream_state> volume_state_context{};
inline constexpr ntl::flt::stream_handle_context<stream_state>
    paged_handle_state_context{ntl::flt::context_pool::paged};
inline constexpr ntl::flt::instance_context<stream_state>
    executable_nonpaged_instance_context{ntl::flt::context_pool::nonpaged};

using file_reference = ntl::flt::context_ref<file_state,
                                              ntl::flt::context_scope::file>;
using volume_definition = ntl::flt::volume_context<stream_state>;

static_assert(!std::is_copy_constructible_v<file_reference>);
static_assert(std::is_nothrow_move_constructible_v<file_reference>);
static_assert(!std::is_copy_constructible_v<ntl::flt::name_information>);
static_assert(
    std::is_nothrow_move_constructible_v<ntl::flt::name_information>);
static_assert(std::is_same_v<
              decltype(std::declval<const ntl::flt::name_information &>()
                           .try_reference()),
              ntl::result<ntl::flt::name_information>>);
static_assert(!std::is_constructible_v<volume_definition,
                                       ntl::flt::context_pool>);
static_assert(std::is_constructible_v<volume_definition, ULONG>);
static_assert(std::is_constructible_v<decltype(file_state_context),
                                      ntl::flt::context_pool>);
static_assert(decltype(file_state_context)::scope ==
              ntl::flt::context_scope::file);
static_assert(decltype(stream_state_context)::scope ==
              ntl::flt::context_scope::stream);
static_assert(file_state_context.pool() ==
              ntl::flt::context_pool::nonpaged_nx);
static_assert(volume_state_context.pool() ==
              ntl::flt::context_pool::nonpaged);
static_assert(paged_handle_state_context.pool() ==
              ntl::flt::context_pool::paged);
static_assert(executable_nonpaged_instance_context.pool() ==
              ntl::flt::context_pool::nonpaged);
static_assert(ntl::flt::detail::native_context_pool(
                  ntl::flt::context_pool::nonpaged) == NonPagedPool);
static_assert(ntl::flt::detail::native_context_pool(
                  ntl::flt::context_pool::nonpaged_nx) == NonPagedPoolNx);
static_assert(ntl::flt::detail::native_context_pool(
                  ntl::flt::context_pool::paged) == PagedPool);

using get_result = decltype(std::declval<ntl::flt::related_objects>().try_get(
    file_state_context));
using create_result = decltype(
    std::declval<ntl::flt::related_objects>().try_get_or_create(
        file_state_context, ULONG{7}));

static_assert(std::is_same_v<get_result, ntl::result<file_reference>>);
static_assert(std::is_same_v<create_result, ntl::result<file_reference>>);

[[maybe_unused]] void compile_registration_surface() {
  ntl::flt::registration callbacks;
  callbacks.context(file_state_context)
      .context(stream_state_context)
      .context(volume_state_context)
      .context(paged_handle_state_context)
      .context(executable_nonpaged_instance_context);
}

} // namespace ntl_flt_context_compile_test
