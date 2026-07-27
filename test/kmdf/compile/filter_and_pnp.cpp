#include <ntl/kmdf/all>

#include <type_traits>
#include <utility>

namespace ntl_kmdf_filter_pnp_compile_test {

constexpr auto prepare =
    +[](ntl::kmdf::device, ntl::kmdf::resource_list,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  return STATUS_SUCCESS;
};
constexpr auto release =
    +[](ntl::kmdf::device,
        ntl::kmdf::resource_list) noexcept -> NTSTATUS {
  return STATUS_SUCCESS;
};
constexpr auto d0_entry =
    +[](ntl::kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  return STATUS_SUCCESS;
};
constexpr auto d0_exit =
    +[](ntl::kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
  return STATUS_SUCCESS;
};
constexpr auto surprise =
    +[](ntl::kmdf::device) noexcept {};

constexpr auto completion =
    +[](ntl::kmdf::request, ntl::kmdf::io_target,
        ntl::kmdf::completion_params, void *) noexcept {};

static_assert(
    std::is_same_v<decltype(std::declval<ntl::kmdf::device_init &>().filter()),
                   ntl::kmdf::device_init &>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::request &>()
                     .on_completion<completion>()),
        ntl::kmdf::request &>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::request &&>().try_send(
            std::declval<const ntl::kmdf::io_target &>())),
        ntl::status>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::request &&>().try_forward_to(
            std::declval<const ntl::kmdf::io_queue &>())),
        ntl::status>);

[[maybe_unused]] void
compile_pnp_and_filter(ntl::kmdf::device_init &init,
                       ntl::kmdf::request request,
                       ntl::kmdf::io_target target) {
  ntl::kmdf::pnp_power_callbacks callbacks;
  callbacks.on_prepare_hardware<prepare>()
      .on_release_hardware<release>()
      .on_d0_entry<d0_entry>()
      .on_d0_exit<d0_exit>()
      .on_surprise_removal<surprise>();
  init.filter().pnp_power(callbacks);

  request.format_current_type();
  request.on_completion<completion>();
  (void)std::move(request).try_send(target);
}

} // namespace ntl_kmdf_filter_pnp_compile_test
