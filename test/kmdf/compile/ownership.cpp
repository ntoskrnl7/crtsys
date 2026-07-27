#include <ntl/kmdf/all>

#include <type_traits>
#include <utility>

namespace ntl_kmdf_ownership_compile_test {

struct sample_interface {
  INTERFACE header;
  NTSTATUS(NTAPI *query)(void *context, ULONG *value) noexcept;
};

static_assert(!std::is_copy_constructible_v<ntl::kmdf::request>);
static_assert(std::is_nothrow_move_constructible_v<ntl::kmdf::request>);
static_assert(!std::is_copy_assignable_v<ntl::kmdf::request>);

static_assert(!std::is_copy_constructible_v<ntl::kmdf::driver_request>);
static_assert(
    std::is_nothrow_move_constructible_v<ntl::kmdf::driver_request>);
static_assert(!std::is_copy_assignable_v<ntl::kmdf::driver_request>);

static_assert(!std::is_copy_constructible_v<ntl::kmdf::found_request>);
static_assert(std::is_nothrow_move_constructible_v<
              ntl::kmdf::found_request>);
static_assert(!std::is_copy_constructible_v<ntl::kmdf::lookaside_memory>);
static_assert(std::is_nothrow_move_constructible_v<
              ntl::kmdf::lookaside_memory>);

using acquired_interface = ntl::kmdf::queried_interface<sample_interface>;
static_assert(!std::is_copy_constructible_v<acquired_interface>);
static_assert(std::is_nothrow_move_constructible_v<acquired_interface>);

static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::io_queue>().try_retrieve_next()),
        ntl::result<ntl::kmdf::request>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::io_queue>().try_find()),
        ntl::result<ntl::kmdf::found_request>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::kmdf::lookaside>().try_allocate()),
        ntl::result<ntl::kmdf::lookaside_memory>>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::kmdf::device>()
                           .try_query_interface<sample_interface>(
                               std::declval<const GUID &>(), 1)),
              ntl::result<acquired_interface>>);

static_assert(sizeof(ntl::kmdf::device) == sizeof(WDFDEVICE));
static_assert(sizeof(ntl::kmdf::io_queue) == sizeof(WDFQUEUE));
static_assert(sizeof(ntl::kmdf::io_target) == sizeof(WDFIOTARGET));
static_assert(sizeof(ntl::kmdf::timer) == sizeof(WDFTIMER));
static_assert(sizeof(ntl::kmdf::interrupt) == sizeof(WDFINTERRUPT));

} // namespace ntl_kmdf_ownership_compile_test
