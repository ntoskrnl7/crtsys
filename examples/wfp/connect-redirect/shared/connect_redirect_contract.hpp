#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_connect_redirect {

using layer_v4 = ntl::wfp::layers::ale_connect_redirect_v4;
using layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;
using layer = layer_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_connect_redirect";

// {6A130E1D-68CA-42A9-B1F4-4ACBA5D17821}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x6a130e1d,
    0x68ca,
    0x42a9,
    {0xb1, 0xf4, 0x4a, 0xcb, 0xa5, 0xd1, 0x78, 0x21}}};

// {BB2DC604-8B1B-4BD1-9E80-A4F2896D4748}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xbb2dc604,
    0x8b1b,
    0x4bd1,
    {0x9e, 0x80, 0xa4, 0xf2, 0x89, 0x6d, 0x47, 0x48}}};

// {8AF0F3B6-8639-4D8C-94DD-19C750622CFA}
inline constexpr ntl::wfp::callout_key<layer> callout_key{GUID{
    0x8af0f3b6,
    0x8639,
    0x4d8c,
    {0x94, 0xdd, 0x19, 0xc7, 0x50, 0x62, 0x2c, 0xfa}}};

// {A341E52B-E2A7-4A2D-96C7-33896916B124}
inline constexpr ntl::wfp::filter_key<layer> filter_key{GUID{
    0xa341e52b,
    0xe2a7,
    0x4a2d,
    {0x96, 0xc7, 0x33, 0x89, 0x69, 0x16, 0xb1, 0x24}}};

// {499987D4-12A6-47B7-AEEB-03A4B0B4CF82}
inline constexpr ntl::wfp::callout_key<layer_v6> callout_key_v6{GUID{
    0x499987d4,
    0x12a6,
    0x47b7,
    {0xae, 0xeb, 0x03, 0xa4, 0xb0, 0xb4, 0xcf, 0x82}}};

// {74ADC62F-547E-4EF1-AC70-AF94C87A70F5}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{GUID{
    0x74adc62f,
    0x547e,
    0x4ef1,
    {0xac, 0x70, 0xaf, 0x94, 0xc8, 0x7a, 0x70, 0xf5}}};

} // namespace wfp_connect_redirect
