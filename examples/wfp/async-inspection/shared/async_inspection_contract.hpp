#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_async_inspection {

using layer_v4 = ntl::wfp::layers::ale_auth_connect_v4;
using layer_v6 = ntl::wfp::layers::ale_auth_connect_v6;

inline constexpr wchar_t service_name[] = L"crtsys_wfp_async_inspection";
inline constexpr std::uint64_t permit_context = 1;
inline constexpr std::uint64_t block_context = 2;

// {F3347BD0-F4D5-413C-9E4A-43C5FAD4D7A3}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0xf3347bd0,
         0xf4d5,
         0x413c,
         {0x9e, 0x4a, 0x43, 0xc5, 0xfa, 0xd4, 0xd7, 0xa3}}};

// {48B0C54D-31FF-485F-8515-3E6AC1FF45D6}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x48b0c54d,
         0x31ff,
         0x485f,
         {0x85, 0x15, 0x3e, 0x6a, 0xc1, 0xff, 0x45, 0xd6}}};

// {2B7DCAD9-F76B-45D9-A088-4A666433201D}
inline constexpr ntl::wfp::callout_key<layer_v4> callout_key_v4{
    GUID{0x2b7dcad9,
         0xf76b,
         0x45d9,
         {0xa0, 0x88, 0x4a, 0x66, 0x64, 0x33, 0x20, 0x1d}}};

// {1B69EED6-184B-4FDF-AC7C-D6291664B1D9}
inline constexpr ntl::wfp::filter_key<layer_v4> permit_filter_key_v4{
    GUID{0x1b69eed6,
         0x184b,
         0x4fdf,
         {0xac, 0x7c, 0xd6, 0x29, 0x16, 0x64, 0xb1, 0xd9}}};

// {BB2FB9B7-ACB3-40CF-8C02-6195963DA436}
inline constexpr ntl::wfp::filter_key<layer_v4> block_filter_key_v4{
    GUID{0xbb2fb9b7,
         0xacb3,
         0x40cf,
         {0x8c, 0x02, 0x61, 0x95, 0x96, 0x3d, 0xa4, 0x36}}};

// {9E38C81D-CE2E-4FF0-A596-D90365463A81}
inline constexpr ntl::wfp::callout_key<layer_v6> callout_key_v6{
    GUID{0x9e38c81d,
         0xce2e,
         0x4ff0,
         {0xa5, 0x96, 0xd9, 0x03, 0x65, 0x46, 0x3a, 0x81}}};

// {A65A70B8-9526-4638-8D85-4073CB47B668}
inline constexpr ntl::wfp::filter_key<layer_v6> permit_filter_key_v6{
    GUID{0xa65a70b8,
         0x9526,
         0x4638,
         {0x8d, 0x85, 0x40, 0x73, 0xcb, 0x47, 0xb6, 0x68}}};

// {C3277F49-D997-4C37-8BA2-F9515116E83D}
inline constexpr ntl::wfp::filter_key<layer_v6> block_filter_key_v6{
    GUID{0xc3277f49,
         0xd997,
         0x4c37,
         {0x8b, 0xa2, 0xf9, 0x51, 0x51, 0x16, 0xe8, 0x3d}}};

} // namespace wfp_async_inspection
