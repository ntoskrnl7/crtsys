#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_tls_inspection_proxy {

using layer = ntl::wfp::layers::ale_connect_redirect_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_tls_inspection_proxy";

// {CB4D7C2A-3BCD-408C-9797-04EAF9060DC1}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0xcb4d7c2a,
    0x3bcd,
    0x408c,
    {0x97, 0x97, 0x04, 0xea, 0xf9, 0x06, 0x0d, 0xc1}}};

// {E5359774-0AF9-42DC-A20E-8294274EF03E}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xe5359774,
    0x0af9,
    0x42dc,
    {0xa2, 0x0e, 0x82, 0x94, 0x27, 0x4e, 0xf0, 0x3e}}};

// {C17EDA23-86CE-4CC7-85A6-75CA3543DE9F}
inline constexpr ntl::wfp::callout_key<layer> callout_key{GUID{
    0xc17eda23,
    0x86ce,
    0x4cc7,
    {0x85, 0xa6, 0x75, 0xca, 0x35, 0x43, 0xde, 0x9f}}};

// {BFE08F43-4E38-4A52-BDAD-5D58DA6CC814}
inline constexpr ntl::wfp::filter_key<layer> filter_key{GUID{
    0xbfe08f43,
    0x4e38,
    0x4a52,
    {0xbd, 0xad, 0x5d, 0x58, 0xda, 0x6c, 0xc8, 0x14}}};

} // namespace wfp_tls_inspection_proxy
