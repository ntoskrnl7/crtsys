#pragma once

#include <guiddef.h>
#include <ntl/wfp/layers>

namespace wfp_datagram_proxy {

using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using datagram_layer = ntl::wfp::layers::datagram_data_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_datagram_proxy";

// {6DDAE357-AC3D-4D57-AEC9-91138C42CCEB}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x6ddae357,
    0xac3d,
    0x4d57,
    {0xae, 0xc9, 0x91, 0x13, 0x8c, 0x42, 0xcc, 0xeb}}};

// {FD8C7C31-1B63-487D-B284-CF6842EC7342}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xfd8c7c31,
    0x1b63,
    0x487d,
    {0xb2, 0x84, 0xcf, 0x68, 0x42, 0xec, 0x73, 0x42}}};

// {DC449686-1265-4295-8A39-AAECE8893F5C}
inline constexpr ntl::wfp::callout_key<flow_layer> flow_callout_key{GUID{
    0xdc449686,
    0x1265,
    0x4295,
    {0x8a, 0x39, 0xaa, 0xec, 0xe8, 0x89, 0x3f, 0x5c}}};

// {E60F6E76-D696-463E-8538-8B46EEE4A02E}
inline constexpr ntl::wfp::callout_key<datagram_layer>
    datagram_callout_key{GUID{
        0xe60f6e76,
        0xd696,
        0x463e,
        {0x85, 0x38, 0x8b, 0x46, 0xee, 0xe4, 0xa0, 0x2e}}};

// {3989D6F8-6D33-43A6-9F0A-0A68AE55C901}
inline constexpr ntl::wfp::filter_key<flow_layer> flow_filter_key{GUID{
    0x3989d6f8,
    0x6d33,
    0x43a6,
    {0x9f, 0x0a, 0x0a, 0x68, 0xae, 0x55, 0xc9, 0x01}}};

// {720A8928-0393-4FF7-BBFA-BF5850365547}
inline constexpr ntl::wfp::filter_key<datagram_layer>
    datagram_filter_key{GUID{
        0x720a8928,
        0x0393,
        0x4ff7,
        {0xbb, 0xfa, 0xbf, 0x58, 0x50, 0x36, 0x55, 0x47}}};

} // namespace wfp_datagram_proxy
