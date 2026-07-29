#pragma once

#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_flow_monitor {

using flow_layer = ntl::wfp::layers::ale_flow_established_v4;
using stream_layer = ntl::wfp::layers::stream_v4;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_flow_monitor";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpFlowMonitor";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpFlowMonitor";

struct monitor_stats {
  std::uint64_t flows_started;
  std::uint64_t flows_closed;
  std::uint64_t stream_indications;
  std::uint64_t stream_bytes;
  std::uint64_t missed_bytes;
};

struct query_stats_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x940;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = monitor_stats;
};

inline constexpr unsigned long query_stats_ioctl =
    CTL_CODE(query_stats_contract::device_type,
             query_stats_contract::function,
             query_stats_contract::method,
             query_stats_contract::access);

// {8928B838-0BE9-4071-A52D-A599E80AF31C}
inline constexpr GUID device_class_guid{
    0x8928b838,
    0x0be9,
    0x4071,
    {0xa5, 0x2d, 0xa5, 0x99, 0xe8, 0x0a, 0xf3, 0x1c}};

// {78DDA968-9366-4AAE-B4C8-339F34CE9E6E}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x78dda968,
    0x9366,
    0x4aae,
    {0xb4, 0xc8, 0x33, 0x9f, 0x34, 0xce, 0x9e, 0x6e}}};

// {E346343F-D19B-470D-B6FE-DD9A88523076}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xe346343f,
    0xd19b,
    0x470d,
    {0xb6, 0xfe, 0xdd, 0x9a, 0x88, 0x52, 0x30, 0x76}}};

// {D2295C99-1253-40EB-AA0B-F1340F7BE317}
inline constexpr ntl::wfp::callout_key<flow_layer> flow_callout_key{GUID{
    0xd2295c99,
    0x1253,
    0x40eb,
    {0xaa, 0x0b, 0xf1, 0x34, 0x0f, 0x7b, 0xe3, 0x17}}};

// {78AB1B5B-4979-4FE7-800B-FF92E6BECAB9}
inline constexpr ntl::wfp::callout_key<stream_layer>
    stream_callout_key{GUID{
        0x78ab1b5b,
        0x4979,
        0x4fe7,
        {0x80, 0x0b, 0xff, 0x92, 0xe6, 0xbe, 0xca, 0xb9}}};

// {99F6631B-3F1A-4836-8973-9177457A93FA}
inline constexpr ntl::wfp::filter_key<flow_layer> flow_filter_key{GUID{
    0x99f6631b,
    0x3f1a,
    0x4836,
    {0x89, 0x73, 0x91, 0x77, 0x45, 0x7a, 0x93, 0xfa}}};

// {AB749AAD-42E9-40AB-9094-58AEE2771492}
inline constexpr ntl::wfp::filter_key<stream_layer>
    stream_filter_key{GUID{
        0xab749aad,
        0x42e9,
        0x40ab,
        {0x90, 0x94, 0x58, 0xae, 0xe2, 0x77, 0x14, 0x92}}};

} // namespace wfp_flow_monitor
