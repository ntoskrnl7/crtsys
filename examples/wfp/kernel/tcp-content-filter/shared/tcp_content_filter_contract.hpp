#pragma once

#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#include "content_filter_record.hpp"

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_tcp_content_filter {

using flow_layer_v4 = ntl::wfp::layers::ale_flow_established_v4;
using flow_layer_v6 = ntl::wfp::layers::ale_flow_established_v6;
using stream_layer_v4 = ntl::wfp::layers::stream_v4;
using stream_layer_v6 = ntl::wfp::layers::stream_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_kernel_tcp_content_filter";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpKernelTcpContentFilter";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelTcpContentFilter";

inline constexpr std::size_t length_prefix_size = 4;
inline constexpr std::size_t maximum_record_body_size = 4096;
inline constexpr std::size_t maximum_record_size =
    crtsys::examples::wfp::content_filter::wire_size(
        maximum_record_body_size);
inline constexpr std::size_t maximum_frame_size =
    length_prefix_size + maximum_record_size;

struct filter_stats {
  std::uint64_t inspected;
  std::uint64_t permitted;
  std::uint64_t blocked;
  std::uint64_t malformed;
  std::uint64_t failed;
};

struct query_stats_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x968;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = filter_stats;
};

inline constexpr unsigned long query_stats_ioctl =
    CTL_CODE(query_stats_contract::device_type, query_stats_contract::function,
             query_stats_contract::method, query_stats_contract::access);

// {A6F12343-3BA8-4CBB-BA63-E5B7AED5E58C}
inline constexpr GUID device_class_guid{
    0xa6f12343, 0x3ba8, 0x4cbb,
    {0xba, 0x63, 0xe5, 0xb7, 0xae, 0xd5, 0xe5, 0x8c}};

// {A6074B51-BA93-4DC8-8517-8CE40476E8CF}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0xa6074b51, 0xba93, 0x4dc8,
         {0x85, 0x17, 0x8c, 0xe4, 0x04, 0x76, 0xe8, 0xcf}}};

// {94D8253A-5546-46D3-8814-C5D88A7FDEB1}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x94d8253a, 0x5546, 0x46d3,
         {0x88, 0x14, 0xc5, 0xd8, 0x8a, 0x7f, 0xde, 0xb1}}};

// {CAC17B18-E455-49AB-9773-89FB3CE943BB}
inline constexpr ntl::wfp::arbitrating_callout_key<flow_layer_v4> flow_callout_key_v4{
    GUID{0xcac17b18, 0xe455, 0x49ab,
         {0x97, 0x73, 0x89, 0xfb, 0x3c, 0xe9, 0x43, 0xbb}}};

// {76E5C209-6550-48AB-A081-65D5B45D8D85}
inline constexpr ntl::wfp::stream_callout_key<stream_layer_v4>
    stream_callout_key_v4{
        GUID{0x76e5c209, 0x6550, 0x48ab,
             {0xa0, 0x81, 0x65, 0xd5, 0xb4, 0x5d, 0x8d, 0x85}}};

// {7787BF2A-27FC-4134-949A-4FAA7F67093E}
inline constexpr ntl::wfp::filter_key<flow_layer_v4> flow_filter_key_v4{
    GUID{0x7787bf2a, 0x27fc, 0x4134,
         {0x94, 0x9a, 0x4f, 0xaa, 0x7f, 0x67, 0x09, 0x3e}}};

// {38685BBF-1303-4DC1-B390-6F328F57155E}
inline constexpr ntl::wfp::filter_key<stream_layer_v4> stream_filter_key_v4{
    GUID{0x38685bbf, 0x1303, 0x4dc1,
         {0xb3, 0x90, 0x6f, 0x32, 0x8f, 0x57, 0x15, 0x5e}}};

// {B758CBB8-BBF4-4A26-AC86-23BA759FE127}
inline constexpr ntl::wfp::arbitrating_callout_key<flow_layer_v6> flow_callout_key_v6{
    GUID{0xb758cbb8, 0xbbf4, 0x4a26,
         {0xac, 0x86, 0x23, 0xba, 0x75, 0x9f, 0xe1, 0x27}}};

// {754DE8E3-E62F-49DC-B96A-9EEABD440779}
inline constexpr ntl::wfp::stream_callout_key<stream_layer_v6>
    stream_callout_key_v6{
        GUID{0x754de8e3, 0xe62f, 0x49dc,
             {0xb9, 0x6a, 0x9e, 0xea, 0xbd, 0x44, 0x07, 0x79}}};

// {56662FD9-B7D6-40C1-B97F-066C2CB8EE2C}
inline constexpr ntl::wfp::filter_key<flow_layer_v6> flow_filter_key_v6{
    GUID{0x56662fd9, 0xb7d6, 0x40c1,
         {0xb9, 0x7f, 0x06, 0x6c, 0x2c, 0xb8, 0xee, 0x2c}}};

// {3B14A467-530F-40DE-A947-13E780FBD80C}
inline constexpr ntl::wfp::filter_key<stream_layer_v6> stream_filter_key_v6{
    GUID{0x3b14a467, 0x530f, 0x40de,
         {0xa9, 0x47, 0x13, 0xe7, 0x80, 0xfb, 0xd8, 0x0c}}};

} // namespace wfp_kernel_tcp_content_filter
