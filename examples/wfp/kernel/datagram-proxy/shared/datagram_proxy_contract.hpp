#pragma once

#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/transparent_udp_proxy_contract>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_datagram_proxy {

using flow_layer_v4 = ntl::wfp::layers::ale_flow_established_v4;
using flow_layer_v6 = ntl::wfp::layers::ale_flow_established_v6;
using datagram_layer_v4 = ntl::wfp::layers::datagram_data_v4;
using datagram_layer_v6 = ntl::wfp::layers::datagram_data_v6;
using reverse_layer_v4 = ntl::wfp::layers::outbound_ip_packet_v4;
using reverse_layer_v6 = ntl::wfp::layers::outbound_ip_packet_v6;

inline constexpr wchar_t service_name[] = L"crtsys_wfp_datagram_proxy";
inline constexpr wchar_t device_name[] = L"CrtSysWfpDatagramProxy";
inline constexpr wchar_t user_device_path[] = L"\\\\.\\CrtSysWfpDatagramProxy";

struct proxy_statistics {
  std::uint64_t outbound_packets;
  std::uint64_t inbound_packets;
  std::uint64_t mapping_updates;
  std::uint64_t mapping_misses;
  std::uint64_t injection_failures;
  std::uint64_t quota_rejections;
  std::uint64_t asynchronous_injection_failures;
  std::int32_t last_asynchronous_injection_status;
  std::uint32_t reserved;
};

struct query_statistics_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x941;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = proxy_statistics;
};

inline constexpr unsigned long query_statistics_ioctl =
    CTL_CODE(query_statistics_contract::device_type,
             query_statistics_contract::function,
             query_statistics_contract::method,
             query_statistics_contract::access);

// {42F3811C-AFE9-4CEB-A9F3-AFC3BB4589F6}
inline constexpr GUID device_class_guid{
    0x42f3811c,
    0xafe9,
    0x4ceb,
    {0xa9, 0xf3, 0xaf, 0xc3, 0xbb, 0x45, 0x89, 0xf6}};

// {6DDAE357-AC3D-4D57-AEC9-91138C42CCEB}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x6ddae357,
         0xac3d,
         0x4d57,
         {0xae, 0xc9, 0x91, 0x13, 0x8c, 0x42, 0xcc, 0xeb}}};

// {FD8C7C31-1B63-487D-B284-CF6842EC7342}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0xfd8c7c31,
         0x1b63,
         0x487d,
         {0xb2, 0x84, 0xcf, 0x68, 0x42, 0xec, 0x73, 0x42}}};

// {DC449686-1265-4295-8A39-AAECE8893F5C}
inline constexpr ntl::wfp::arbitrating_callout_key<flow_layer_v4> flow_callout_key_v4{
    GUID{0xdc449686,
         0x1265,
         0x4295,
         {0x8a, 0x39, 0xaa, 0xec, 0xe8, 0x89, 0x3f, 0x5c}}};

// {E60F6E76-D696-463E-8538-8B46EEE4A02E}
inline constexpr ntl::wfp::terminating_callout_key<datagram_layer_v4>
    datagram_callout_key_v4{
        GUID{0xe60f6e76,
             0xd696,
             0x463e,
             {0x85, 0x38, 0x8b, 0x46, 0xee, 0xe4, 0xa0, 0x2e}}};

// {3989D6F8-6D33-43A6-9F0A-0A68AE55C901}
inline constexpr ntl::wfp::filter_key<flow_layer_v4> flow_filter_key_v4{
    GUID{0x3989d6f8,
         0x6d33,
         0x43a6,
         {0x9f, 0x0a, 0x0a, 0x68, 0xae, 0x55, 0xc9, 0x01}}};

// {720A8928-0393-4FF7-BBFA-BF5850365547}
inline constexpr ntl::wfp::filter_key<datagram_layer_v4> datagram_filter_key_v4{
    GUID{0x720a8928,
         0x0393,
         0x4ff7,
         {0xbb, 0xfa, 0xbf, 0x58, 0x50, 0x36, 0x55, 0x47}}};

// {128D8DCA-B6E9-4EC5-9C6D-B7C7D39F64F6}
inline constexpr ntl::wfp::arbitrating_callout_key<flow_layer_v6> flow_callout_key_v6{
    GUID{0x128d8dca,
         0xb6e9,
         0x4ec5,
         {0x9c, 0x6d, 0xb7, 0xc7, 0xd3, 0x9f, 0x64, 0xf6}}};

// {66006323-2CDC-4D55-A26F-7139ADC1487C}
inline constexpr ntl::wfp::terminating_callout_key<datagram_layer_v6>
    datagram_callout_key_v6{
        GUID{0x66006323,
             0x2cdc,
             0x4d55,
             {0xa2, 0x6f, 0x71, 0x39, 0xad, 0xc1, 0x48, 0x7c}}};

// {3EAA1D1D-3D97-425D-B521-E67813016012}
inline constexpr ntl::wfp::filter_key<flow_layer_v6> flow_filter_key_v6{
    GUID{0x3eaa1d1d,
         0x3d97,
         0x425d,
         {0xb5, 0x21, 0xe6, 0x78, 0x13, 0x01, 0x60, 0x12}}};

// {C3300AE4-7783-4761-BA52-CA5D90E465D2}
inline constexpr ntl::wfp::filter_key<datagram_layer_v6> datagram_filter_key_v6{
    GUID{0xc3300ae4,
         0x7783,
         0x4761,
         {0xba, 0x52, 0xca, 0x5d, 0x90, 0xe4, 0x65, 0xd2}}};

// {242E18FD-F866-41D2-92A9-29682BE55A13}
inline constexpr ntl::wfp::terminating_callout_key<reverse_layer_v4>
    reverse_callout_key_v4{
        GUID{0x242e18fd,
             0xf866,
             0x41d2,
             {0x92, 0xa9, 0x29, 0x68, 0x2b, 0xe5, 0x5a, 0x13}}};

// {68CF0ED9-4BF9-471C-B27C-9C1666A8A799}
inline constexpr ntl::wfp::filter_key<reverse_layer_v4>
    reverse_filter_key_v4{
        GUID{0x68cf0ed9,
             0x4bf9,
             0x471c,
             {0xb2, 0x7c, 0x9c, 0x16, 0x66, 0xa8, 0xa7, 0x99}}};

// {3708A7E1-5E86-4AA0-9D7D-3293F7784070}
inline constexpr ntl::wfp::terminating_callout_key<reverse_layer_v6>
    reverse_callout_key_v6{
        GUID{0x3708a7e1,
             0x5e86,
             0x4aa0,
             {0x9d, 0x7d, 0x32, 0x93, 0xf7, 0x78, 0x40, 0x70}}};

// {14196362-EAA7-4218-B719-834FCCC8EE35}
inline constexpr ntl::wfp::filter_key<reverse_layer_v6>
    reverse_filter_key_v6{
        GUID{0x14196362,
             0xeaa7,
             0x4218,
             {0xb7, 0x19, 0x83, 0x4f, 0xcc, 0xc8, 0xee, 0x35}}};

inline constexpr ntl::wfp::transparent_udp_proxy_keys proxy_keys{
    provider_key,
    sublayer_key,
    flow_callout_key_v4,
    flow_callout_key_v6,
    datagram_callout_key_v4,
    datagram_callout_key_v6,
    reverse_callout_key_v4,
    reverse_callout_key_v6,
    flow_filter_key_v4,
    flow_filter_key_v6,
    datagram_filter_key_v4,
    datagram_filter_key_v6,
    reverse_filter_key_v4,
    reverse_filter_key_v6};

} // namespace wfp_datagram_proxy
