/**
 * @file instance.cpp
 * @author jungkwang.lee (ntoskrnl7@gmail.com)
 * @brief Compile-time coverage for NTL minifilter instance management.
 *
 * @copyright Copyright (c) 2022 NT Template Library Authors.
 */

#include <ntl/flt/all>

#include <type_traits>
#include <utility>
#include <vector>

namespace ntl_flt_instance_compile_test {

using information_result =
    decltype(std::declval<ntl::flt::instance>().try_information());
using attach_result = decltype(std::declval<ntl::flt::filter>().try_attach(
    std::declval<ntl::flt::volume>()));
using named_attach_result =
    decltype(std::declval<ntl::flt::filter>().try_attach(
        std::declval<ntl::flt::volume>(), std::wstring_view{}));
using altitude_attach_result =
    decltype(std::declval<ntl::flt::filter>().try_attach_at(
        std::declval<ntl::flt::volume>(), std::wstring_view{}));
using enumerate_result =
    decltype(std::declval<ntl::flt::filter>().try_instances());

static_assert(std::is_same_v<information_result,
                             ntl::result<ntl::flt::instance_information>>);
static_assert(
    std::is_same_v<attach_result, ntl::result<ntl::flt::instance_ref>>);
static_assert(
    std::is_same_v<named_attach_result, ntl::result<ntl::flt::instance_ref>>);
static_assert(std::is_same_v<altitude_attach_result,
                             ntl::result<ntl::flt::instance_ref>>);
static_assert(
    std::is_same_v<enumerate_result,
                   ntl::result<std::vector<ntl::flt::instance_information>>>);
static_assert(!std::is_copy_constructible_v<ntl::flt::instance_ref>);
static_assert(std::is_nothrow_move_constructible_v<ntl::flt::instance_ref>);

} // namespace ntl_flt_instance_compile_test
