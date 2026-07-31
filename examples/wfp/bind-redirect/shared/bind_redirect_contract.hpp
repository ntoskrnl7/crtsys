#pragma once

#include <guiddef.h>
#include <ntl/wfp/connect_redirect>
#include <ntl/wfp/layers>

namespace wfp_bind_redirect {

using layer_v4 = ntl::wfp::layers::ale_bind_redirect_v4;
using layer_v6 = ntl::wfp::layers::ale_bind_redirect_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_bind_redirect";
inline constexpr std::uint16_t redirected_port_v4 = 47131;
inline constexpr std::uint16_t redirected_port_v6 = 47132;
inline constexpr ntl::wfp::bind_redirect_selector selector_v4{1};
inline constexpr ntl::wfp::bind_redirect_selector selector_v6{2};

// {D8B710A5-41D1-430C-B2B3-AC4DEDCE2BEE}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0xd8b710a5,
    0x41d1,
    0x430c,
    {0xb2, 0xb3, 0xac, 0x4d, 0xed, 0xce, 0x2b, 0xee}}};

// {218B315E-9D44-4ABC-9639-1EFEFD50C6BA}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0x218b315e,
    0x9d44,
    0x4abc,
    {0x96, 0x39, 0x1e, 0xfe, 0xfd, 0x50, 0xc6, 0xba}}};

// {2E3520A8-FD60-438E-AAE7-399718518251}
inline constexpr ntl::wfp::callout_key<layer_v4> callout_key_v4{GUID{
    0x2e3520a8,
    0xfd60,
    0x438e,
    {0xaa, 0xe7, 0x39, 0x97, 0x18, 0x51, 0x82, 0x51}}};

// {B1D768C2-B20F-4D32-A5ED-287EB498EB84}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{GUID{
    0xb1d768c2,
    0xb20f,
    0x4d32,
    {0xa5, 0xed, 0x28, 0x7e, 0xb4, 0x98, 0xeb, 0x84}}};

// {F0E07DBD-EB39-45A7-AE94-4AE974768534}
inline constexpr ntl::wfp::callout_key<layer_v6> callout_key_v6{GUID{
    0xf0e07dbd,
    0xeb39,
    0x45a7,
    {0xae, 0x94, 0x4a, 0xe9, 0x74, 0x76, 0x85, 0x34}}};

// {2D2791F1-C8D8-4E54-B567-2835D558EA75}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{GUID{
    0x2d2791f1,
    0xc8d8,
    0x4e54,
    {0xb5, 0x67, 0x28, 0x35, 0xd5, 0x58, 0xea, 0x75}}};

} // namespace wfp_bind_redirect
