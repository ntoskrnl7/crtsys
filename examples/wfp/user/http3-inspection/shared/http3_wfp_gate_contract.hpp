#pragma once

#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#if defined(_KERNEL_MODE) || defined(_KERNEL32_)
#include <wdm.h>
#else
#include <winioctl.h>
#endif

#include <ntl/wfp/layers>

namespace wfp_user_http3_inspection {

using layer_v4 = ntl::wfp::layers::ale_auth_connect_v4;
using layer_v6 = ntl::wfp::layers::ale_auth_connect_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_http3_inspection_driver";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpUserHttp3Inspection";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpUserHttp3Inspection";
inline constexpr std::uint32_t telemetry_version = 1;

inline std::uint64_t hash_application_id(
    const std::uint8_t *data, std::size_t size) noexcept {
  constexpr std::uint64_t offset = 14695981039346656037ull;
  constexpr std::uint64_t prime = 1099511628211ull;
  std::uint64_t result = offset;
  for (std::size_t index = 0; index != size; ++index) {
    result ^= data[index];
    result *= prime;
  }
  return result;
}

struct alignas(8) layer_telemetry {
  std::uint64_t classify_hits;
  std::uint64_t permit_decisions;
  std::uint64_t invalid_protocol;
  std::uint64_t action_write_available;
  std::uint64_t action_write_missing;
  std::uint64_t last_filter_id;
  std::uint64_t last_process_id;
  std::uint64_t last_application_id_hash;
  std::uint32_t last_application_id_size;
  std::uint32_t last_remote_address_v4;
  std::uint32_t last_remote_address_v6[4];
  std::uint16_t last_remote_port;
  std::uint16_t last_filter_flags;
  std::uint8_t last_protocol;
  std::uint8_t address_family;
  std::uint16_t reserved;
};

struct alignas(8) gate_telemetry {
  std::uint32_t version;
  std::uint32_t size;
  layer_telemetry ipv4;
  layer_telemetry ipv6;
};

struct query_telemetry_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x974;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = gate_telemetry;
};

inline constexpr unsigned long query_telemetry_ioctl =
    CTL_CODE(query_telemetry_contract::device_type,
             query_telemetry_contract::function,
             query_telemetry_contract::method,
             query_telemetry_contract::access);

static_assert(sizeof(layer_telemetry) == 96);
static_assert(sizeof(gate_telemetry) == 200);

inline constexpr GUID device_class_guid{
    0xb77be865, 0xfb62, 0x4fa6,
    {0xbd, 0x30, 0xa0, 0x8f, 0x76, 0x41, 0x42, 0xb0}};

inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0x56899e2d, 0x6d04, 0x4681,
    {0x8a, 0x60, 0x54, 0x0d, 0x68, 0x30, 0xb4, 0xc4}}};
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xd9ce4f21, 0x024a, 0x416f,
    {0xa3, 0x2a, 0x2c, 0xca, 0xee, 0xd4, 0xe8, 0x8e}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{GUID{
    0x4c05143e, 0x4830, 0x4362,
    {0x93, 0x58, 0x9b, 0xc9, 0x36, 0xcb, 0xbf, 0x8c}}};
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{GUID{
    0xfb5ea90d, 0x14fb, 0x417f,
    {0xb6, 0xdc, 0xd6, 0xb4, 0xff, 0x98, 0x7d, 0xe6}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{GUID{
    0x93fb23e2, 0xede0, 0x4d12,
    {0x97, 0x93, 0x67, 0xba, 0x6c, 0x3d, 0x6b, 0x28}}};
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{GUID{
    0xf1bbc385, 0xfed8, 0x4bde,
    {0xb2, 0x57, 0xa5, 0x05, 0x49, 0xf3, 0xa5, 0xc2}}};

// The unavailable-callout proof deliberately uses a disjoint policy graph.
// The driver never registers either of these callout keys.
inline constexpr ntl::wfp::provider_key unavailable_provider_key{GUID{
    0x0fa6acee, 0x0d27, 0x4c3f,
    {0xa9, 0xcf, 0x5f, 0xd4, 0x7a, 0xa7, 0x53, 0x16}}};
inline constexpr ntl::wfp::sublayer_key unavailable_sublayer_key{GUID{
    0xfa1c47fb, 0xba7c, 0x4b31,
    {0x85, 0x83, 0x40, 0x9f, 0x4f, 0x7a, 0x8c, 0x99}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v4>
    unavailable_callout_key_v4{GUID{
        0xf45ce3b1, 0xd200, 0x48c0,
        {0xa6, 0x32, 0x0c, 0x7b, 0x91, 0xf9, 0x06, 0xa2}}};
inline constexpr ntl::wfp::filter_key<layer_v4>
    unavailable_filter_key_v4{GUID{
        0xc40757d3, 0x9893, 0x4f2e,
        {0xa6, 0x78, 0xa0, 0x17, 0xa4, 0xe1, 0x46, 0xa8}}};
inline constexpr ntl::wfp::terminating_callout_key<layer_v6>
    unavailable_callout_key_v6{GUID{
        0xb45231b3, 0x0105, 0x42c2,
        {0x8c, 0xb4, 0x9a, 0xe6, 0xb5, 0x39, 0x2f, 0x78}}};
inline constexpr ntl::wfp::filter_key<layer_v6>
    unavailable_filter_key_v6{GUID{
        0xa84dfc8d, 0x45b4, 0x441f,
        {0x99, 0x39, 0xe5, 0x1e, 0x36, 0xe8, 0x0b, 0xd0}}};

} // namespace wfp_user_http3_inspection
