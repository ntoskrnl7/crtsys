#pragma once

#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#include "content_filter_record.hpp"

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_udp_content_filter {

using layer_v4 = ntl::wfp::layers::datagram_data_v4;
using layer_v6 = ntl::wfp::layers::datagram_data_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_kernel_udp_content_filter";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpKernelUdpContentFilter";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelUdpContentFilter";
inline constexpr std::size_t maximum_record_body_size = 4096;
inline constexpr std::size_t maximum_record_size =
    crtsys::examples::wfp::content_filter::wire_size(
        maximum_record_body_size);

struct filter_stats {
  std::uint64_t inspected;
  std::uint64_t permitted;
  std::uint64_t blocked;
  std::uint64_t malformed;
  std::uint64_t failed;
};

struct query_stats_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x969;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = filter_stats;
};
inline constexpr unsigned long query_stats_ioctl =
    CTL_CODE(query_stats_contract::device_type, query_stats_contract::function,
             query_stats_contract::method, query_stats_contract::access);

// {51129168-0077-43C8-BCE9-EB80446B349B}
inline constexpr GUID device_class_guid{
    0x51129168, 0x0077, 0x43c8,
    {0xbc, 0xe9, 0xeb, 0x80, 0x44, 0x6b, 0x34, 0x9b}};

// {8C2AD358-E888-4514-BEA5-12FA6122F3DE}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x8c2ad358, 0xe888, 0x4514,
         {0xbe, 0xa5, 0x12, 0xfa, 0x61, 0x22, 0xf3, 0xde}}};

// {53A216D6-9BB4-431A-8051-7E33ADB1AB70}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x53a216d6, 0x9bb4, 0x431a,
         {0x80, 0x51, 0x7e, 0x33, 0xad, 0xb1, 0xab, 0x70}}};

// {BBE031DE-F15F-466C-82B1-7F363E248409}
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{
    GUID{0xbbe031de, 0xf15f, 0x466c,
         {0x82, 0xb1, 0x7f, 0x36, 0x3e, 0x24, 0x84, 0x09}}};

// {93A1DBCC-AAD6-4111-8B40-FAA230DB31EE}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{
    GUID{0x93a1dbcc, 0xaad6, 0x4111,
         {0x8b, 0x40, 0xfa, 0xa2, 0x30, 0xdb, 0x31, 0xee}}};

// {74819E95-0630-4291-9AAB-4CAD96809A7F}
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{
    GUID{0x74819e95, 0x0630, 0x4291,
         {0x9a, 0xab, 0x4c, 0xad, 0x96, 0x80, 0x9a, 0x7f}}};

// {8C55C28F-5780-4F6F-A392-793F022FAF41}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{
    GUID{0x8c55c28f, 0x5780, 0x4f6f,
         {0xa3, 0x92, 0x79, 0x3f, 0x02, 0x2f, 0xaf, 0x41}}};

} // namespace wfp_kernel_udp_content_filter
