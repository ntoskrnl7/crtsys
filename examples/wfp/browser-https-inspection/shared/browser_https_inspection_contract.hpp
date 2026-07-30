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

namespace wfp_browser_https_inspection {

using layer_v4 = ntl::wfp::layers::ale_connect_redirect_v4;
using layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;
using quic_layer_v4 = ntl::wfp::layers::ale_auth_connect_v4;
using quic_layer_v6 = ntl::wfp::layers::ale_auth_connect_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_browser_https_inspection";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpBrowserHttpsInspection";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpBrowserHttpsInspection";

inline constexpr std::uint32_t telemetry_version = 1;

inline std::uint64_t hash_application_id(
    const std::uint8_t *data, std::size_t size) noexcept {
  constexpr std::uint64_t offset =
      14695981039346656037ull;
  constexpr std::uint64_t prime = 1099511628211ull;
  std::uint64_t result = offset;
  for (std::size_t index = 0; index != size; ++index) {
    result ^= data[index];
    result *= prime;
  }
  return result;
}

struct alignas(8) quic_layer_telemetry {
  std::uint64_t classify_hits;
  std::uint64_t block_decisions;
  std::uint64_t action_write_available;
  std::uint64_t action_write_missing;
  std::uint64_t initial_permit;
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

struct alignas(8) quic_telemetry {
  std::uint32_t version;
  std::uint32_t size;
  quic_layer_telemetry ipv4;
  quic_layer_telemetry ipv6;
};

struct query_telemetry_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x945;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = quic_telemetry;
};

inline constexpr unsigned long query_telemetry_ioctl =
    CTL_CODE(query_telemetry_contract::device_type,
             query_telemetry_contract::function,
             query_telemetry_contract::method,
             query_telemetry_contract::access);

static_assert(sizeof(quic_layer_telemetry) == 96);
static_assert(sizeof(quic_telemetry) == 200);

// {007C33EA-42FB-46A5-870D-D50108E34B4E}
inline constexpr GUID device_class_guid{
    0x007c33ea,
    0x42fb,
    0x46a5,
    {0x87, 0x0d, 0xd5, 0x01, 0x08, 0xe3, 0x4b, 0x4e}};

// {D2DC0D78-E0DA-4509-9CBA-7EFC7FB7F3F8}
inline constexpr ntl::wfp::provider_key provider_key{GUID{
    0xd2dc0d78,
    0xe0da,
    0x4509,
    {0x9c, 0xba, 0x7e, 0xfc, 0x7f, 0xb7, 0xf3, 0xf8}}};

// {D893333E-6316-4699-BF8D-DE624159D381}
inline constexpr ntl::wfp::sublayer_key sublayer_key{GUID{
    0xd893333e,
    0x6316,
    0x4699,
    {0xbf, 0x8d, 0xde, 0x62, 0x41, 0x59, 0xd3, 0x81}}};

// {32CED9FF-B52D-4033-8458-F6789EA0FF12}
inline constexpr ntl::wfp::callout_key<layer_v4> callout_key_v4{GUID{
    0x32ced9ff,
    0xb52d,
    0x4033,
    {0x84, 0x58, 0xf6, 0x78, 0x9e, 0xa0, 0xff, 0x12}}};

// {E70BC514-972C-4745-AE49-DD066F55DFA1}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{GUID{
    0xe70bc514,
    0x972c,
    0x4745,
    {0xae, 0x49, 0xdd, 0x06, 0x6f, 0x55, 0xdf, 0xa1}}};

// {AB71DCF2-E65C-4925-97E2-D36DF9EC397A}
inline constexpr ntl::wfp::callout_key<layer_v6> callout_key_v6{GUID{
    0xab71dcf2,
    0xe65c,
    0x4925,
    {0x97, 0xe2, 0xd3, 0x6d, 0xf9, 0xec, 0x39, 0x7a}}};

// {DFE54787-5660-460F-AC3A-C15CDA1A1FD4}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{GUID{
    0xdfe54787,
    0x5660,
    0x460f,
    {0xac, 0x3a, 0xc1, 0x5c, 0xda, 0x1a, 0x1f, 0xd4}}};

// {975E816D-CB27-4356-A9B3-D542AC2AD8C5}
inline constexpr ntl::wfp::filter_key<layer_v4>
    quic_redirect_filter_key_v4{GUID{
        0x975e816d,
        0xcb27,
        0x4356,
        {0xa9, 0xb3, 0xd5, 0x42, 0xac, 0x2a, 0xd8, 0xc5}}};

// {BE4D66F0-0B0E-46AD-9C00-B6F28513F89D}
inline constexpr ntl::wfp::filter_key<layer_v6>
    quic_redirect_filter_key_v6{GUID{
        0xbe4d66f0,
        0x0b0e,
        0x46ad,
        {0x9c, 0x00, 0xb6, 0xf2, 0x85, 0x13, 0xf8, 0x9d}}};

// {5B977D9D-B5E9-43BB-81F8-856EB4F613A0}
inline constexpr ntl::wfp::callout_key<quic_layer_v4>
    quic_callout_key_v4{GUID{
        0x5b977d9d,
        0xb5e9,
        0x43bb,
        {0x81, 0xf8, 0x85, 0x6e, 0xb4, 0xf6, 0x13, 0xa0}}};

// {FBE71374-BBFB-49C1-B6CB-70C2A95FB058}
inline constexpr ntl::wfp::filter_key<quic_layer_v4>
    quic_filter_key_v4{GUID{
        0xfbe71374,
        0xbbfb,
        0x49c1,
        {0xb6, 0xcb, 0x70, 0xc2, 0xa9, 0x5f, 0xb0, 0x58}}};

// {9CF5B26A-8981-42CD-9C09-F8BDC32B7314}
inline constexpr ntl::wfp::callout_key<quic_layer_v6>
    quic_callout_key_v6{GUID{
        0x9cf5b26a,
        0x8981,
        0x42cd,
        {0x9c, 0x09, 0xf8, 0xbd, 0xc3, 0x2b, 0x73, 0x14}}};

// {1482AC10-BCA5-4592-B611-293286712187}
inline constexpr ntl::wfp::filter_key<quic_layer_v6>
    quic_filter_key_v6{GUID{
        0x1482ac10,
        0xbca5,
        0x4592,
        {0xb6, 0x11, 0x29, 0x32, 0x86, 0x71, 0x21, 0x87}}};

} // namespace wfp_browser_https_inspection
