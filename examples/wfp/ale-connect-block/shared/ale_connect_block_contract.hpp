#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_ale_connect_block {

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_ale_connect_block";
inline constexpr unsigned short default_port = 38471;

// {996BEB71-78A9-43A1-96EF-C578F3BC1B41}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x996beb71,
    0x78a9,
    0x43a1,
    {0x96, 0xef, 0xc5, 0x78, 0xf3, 0xbc, 0x1b, 0x41}}};

// {42EAFF40-DB21-4F16-A192-72A28DCD5D4F}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0x42eaff40,
    0xdb21,
    0x4f16,
    {0xa1, 0x92, 0x72, 0xa2, 0x8d, 0xcd, 0x5d, 0x4f}}};

// {A33D611C-613A-43A6-AB2B-D12A7C9D856A}
inline constexpr ntl::wfp::callout_key<
    ntl::wfp::layers::ale_auth_connect_v4>
    callout_key{GUID{
    0xa33d611c,
    0x613a,
    0x43a6,
    {0xab, 0x2b, 0xd1, 0x2a, 0x7c, 0x9d, 0x85, 0x6a}}};

// {4CCF1D42-FAF2-47D1-A19D-77822DD4D977}
inline constexpr ntl::wfp::filter_key<
    ntl::wfp::layers::ale_auth_connect_v4>
    filter_key{GUID{
    0x4ccf1d42,
    0xfaf2,
    0x47d1,
    {0xa1, 0x9d, 0x77, 0x82, 0x2d, 0xd4, 0xd9, 0x77}}};

// {E7D94C06-873C-4A0B-B894-4C32D21EF0D1}
inline constexpr ntl::wfp::filter_key<
    ntl::wfp::layers::ale_auth_connect_v4>
    boot_filter_key{GUID{
    0xe7d94c06,
    0x873c,
    0x4a0b,
    {0xb8, 0x94, 0x4c, 0x32, 0xd2, 0x1e, 0xf0, 0xd1}}};

inline constexpr ntl::wfp::provider_key arbitration_permit_provider_key{GUID{
    0x57b72f01,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};
inline constexpr ntl::wfp::sublayer_key arbitration_permit_sublayer_key{GUID{
    0x57b72f02,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};
inline constexpr ntl::wfp::filter_key<
    ntl::wfp::layers::ale_auth_connect_v4>
    arbitration_permit_filter_key{GUID{
    0x57b72f03,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};

inline constexpr ntl::wfp::provider_key arbitration_block_provider_key{GUID{
    0x57b72f11,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};
inline constexpr ntl::wfp::sublayer_key arbitration_block_sublayer_key{GUID{
    0x57b72f12,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};
inline constexpr ntl::wfp::filter_key<
    ntl::wfp::layers::ale_auth_connect_v4>
    arbitration_block_filter_key{GUID{
    0x57b72f13,
    0xd5bb,
    0x4970,
    {0x87, 0x4e, 0x11, 0xa9, 0xed, 0x68, 0x7d, 0x20}}};

} // namespace wfp_ale_connect_block
