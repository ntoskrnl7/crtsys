#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_async_inspection {

using layer = ntl::wfp::layers::ale_auth_connect_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_async_inspection";
inline constexpr std::uint64_t permit_context = 1;
inline constexpr std::uint64_t block_context = 2;

// {F3347BD0-F4D5-413C-9E4A-43C5FAD4D7A3}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0xf3347bd0,
    0xf4d5,
    0x413c,
    {0x9e, 0x4a, 0x43, 0xc5, 0xfa, 0xd4, 0xd7, 0xa3}}};

// {48B0C54D-31FF-485F-8515-3E6AC1FF45D6}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0x48b0c54d,
    0x31ff,
    0x485f,
    {0x85, 0x15, 0x3e, 0x6a, 0xc1, 0xff, 0x45, 0xd6}}};

// {2B7DCAD9-F76B-45D9-A088-4A666433201D}
inline constexpr ntl::wfp::callout_key<layer> callout_key{GUID{
    0x2b7dcad9,
    0xf76b,
    0x45d9,
    {0xa0, 0x88, 0x4a, 0x66, 0x64, 0x33, 0x20, 0x1d}}};

// {1B69EED6-184B-4FDF-AC7C-D6291664B1D9}
inline constexpr ntl::wfp::filter_key<layer> permit_filter_key{GUID{
    0x1b69eed6,
    0x184b,
    0x4fdf,
    {0xac, 0x7c, 0xd6, 0x29, 0x16, 0x64, 0xb1, 0xd9}}};

// {BB2FB9B7-ACB3-40CF-8C02-6195963DA436}
inline constexpr ntl::wfp::filter_key<layer> block_filter_key{GUID{
    0xbb2fb9b7,
    0xacb3,
    0x40cf,
    {0x8c, 0x02, 0x61, 0x95, 0x96, 0x3d, 0xa4, 0x36}}};

} // namespace wfp_async_inspection
