#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_http3_inspection {

using layer_v4 = ntl::wfp::layers::ale_auth_connect_v4;
using layer_v6 = ntl::wfp::layers::ale_auth_connect_v6;

inline constexpr wchar_t service_name[] = L"crtsys_wfp_kernel_http3_inspection";
inline constexpr wchar_t device_name[] = L"CrtSysWfpKernelHttp3Inspection";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelHttp3Inspection";
inline constexpr std::size_t certificate_thumbprint_size = 20;
inline constexpr std::size_t maximum_capture_size = 4096;

struct certificate_config {
  std::array<std::byte, certificate_thumbprint_size> sha1_thumbprint;
};

struct service_info {
  std::uint16_t port;
  std::uint16_t reserved;
  std::uint32_t ready;
  std::uint64_t wfp_ipv4;
  std::uint64_t wfp_ipv6;
  std::uint64_t accepted;
  std::uint64_t permitted;
  std::uint64_t blocked;
  std::uint64_t failed;
  std::uint64_t qpack_resumed;
  std::uint64_t gzip_responses;
  std::uint64_t deflate_responses;
  std::uint64_t brotli_responses;
  std::uint64_t webtransport_sessions;
  std::uint64_t webtransport_bidirectional;
  std::uint64_t webtransport_unidirectional;
  std::uint64_t webtransport_datagrams;
  std::uint64_t webtransport_capsules;
  std::uint64_t webtransport_resets;
  std::uint64_t active_connections;
  std::uint64_t peak_connections;
  std::uint64_t reaped_connections;
};

struct inspection_record {
  std::uint64_t sequence;
  std::uint32_t status;
  std::uint32_t request_size;
  std::uint32_t response_size;
  std::array<std::byte, maximum_capture_size> request;
  std::array<std::byte, maximum_capture_size> response;
};

template <unsigned long Function, class Input, class Output>
struct ioctl_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = Function;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  using input_type = Input;
  using output_type = Output;
};

using configure_contract = ioctl_contract<0x970, certificate_config, void>;
using query_contract = ioctl_contract<0x971, void, service_info>;
using capture_contract = ioctl_contract<0x972, void, inspection_record>;

inline constexpr unsigned long configure_ioctl =
    CTL_CODE(configure_contract::device_type, configure_contract::function,
             configure_contract::method, configure_contract::access);
inline constexpr unsigned long query_ioctl =
    CTL_CODE(query_contract::device_type, query_contract::function,
             query_contract::method, query_contract::access);
inline constexpr unsigned long capture_ioctl =
    CTL_CODE(capture_contract::device_type, capture_contract::function,
             capture_contract::method, capture_contract::access);

inline constexpr GUID device_class_guid{
    0xb0abbbca, 0x479f, 0x4ec0,
    {0x89, 0x04, 0x9f, 0xeb, 0xc1, 0xef, 0xa3, 0xfe}};
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x8c66fd7d, 0xdb8a, 0x4e07,
         {0x97, 0x3b, 0x52, 0x2d, 0xb3, 0x83, 0xff, 0x9f}}};
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0xf9fd484e, 0x709e, 0x4904,
         {0x8e, 0x3c, 0x8f, 0x34, 0x7e, 0xe3, 0xb2, 0x73}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{
    GUID{0x22e006af, 0xcb27, 0x4db0,
         {0xb7, 0x06, 0x32, 0xc4, 0x8c, 0x3c, 0xfc, 0xbd}}};
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{
    GUID{0xf893a11f, 0xff02, 0x4a73,
         {0x88, 0xb2, 0xf9, 0xa0, 0xfb, 0x62, 0x53, 0x23}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{
    GUID{0xa8eed8e7, 0xab13, 0x4083,
         {0xb5, 0xe6, 0x6a, 0x7d, 0xd7, 0x92, 0x48, 0xc2}}};
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{
    GUID{0x34aa5d7d, 0x7b0e, 0x4e3d,
         {0x8d, 0x2f, 0x77, 0x76, 0x6f, 0x45, 0x5a, 0x88}}};

// Deliberately never registered by the driver.  The acceptance app uses
// these distinct keys to prove callout_unavailable::block on both families.
inline constexpr ntl::wfp::provider_key unavailable_provider_key{
    GUID{0xac3e7b9e, 0xfe47, 0x4d45,
         {0xa6, 0x03, 0xcd, 0x62, 0x18, 0xbd, 0x5b, 0x16}}};
inline constexpr ntl::wfp::sublayer_key unavailable_sublayer_key{
    GUID{0x0ba3726e, 0xb432, 0x48da,
         {0x8f, 0x97, 0x5e, 0x88, 0x99, 0xf3, 0x7c, 0x4c}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> unavailable_callout_key_v4{
    GUID{0x032635b5, 0xabea, 0x4942,
         {0xbe, 0xf1, 0xea, 0x10, 0x6b, 0xfe, 0x2b, 0x42}}};
inline constexpr ntl::wfp::filter_key<layer_v4> unavailable_filter_key_v4{
    GUID{0xf6298043, 0x898b, 0x46d4,
         {0xbe, 0x63, 0x05, 0x4a, 0xa5, 0x3e, 0xe2, 0xd7}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> unavailable_callout_key_v6{
    GUID{0x8d0ec5e7, 0xa2da, 0x43ae,
         {0xa1, 0xbf, 0x6e, 0xbe, 0x8f, 0xef, 0x9e, 0x60}}};
inline constexpr ntl::wfp::filter_key<layer_v6> unavailable_filter_key_v6{
    GUID{0x12184097, 0xf9ce, 0x4697,
         {0xb4, 0x70, 0x88, 0xfa, 0x65, 0xf6, 0x9e, 0x06}}};

inline constexpr GUID msquic_module_id{
    0xc9bd3e68, 0xeb1f, 0x4a20,
    {0xb5, 0x5d, 0x80, 0x7c, 0xbe, 0x9d, 0xb4, 0x2b}};

} // namespace wfp_kernel_http3_inspection
