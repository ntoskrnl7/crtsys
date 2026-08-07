#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <guiddef.h>
#include <ntl/wfp/layers>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace wfp_kernel_tls_inspection_proxy {

using layer_v4 = ntl::wfp::layers::ale_connect_redirect_v4;
using layer_v6 = ntl::wfp::layers::ale_connect_redirect_v6;

inline constexpr wchar_t service_name[] =
    L"crtsys_wfp_kernel_tls_inspection_proxy";
inline constexpr wchar_t device_name[] =
    L"CrtSysWfpKernelTlsInspectionProxy";
inline constexpr wchar_t user_device_path[] =
    L"\\\\.\\CrtSysWfpKernelTlsInspectionProxy";
inline constexpr std::size_t certificate_thumbprint_size = 20;
inline constexpr std::size_t maximum_server_name_size = 253;
inline constexpr std::size_t maximum_http_message_size = 64 * 1024;
inline constexpr std::size_t maximum_http_body_size = 32 * 1024;
inline constexpr std::size_t maximum_capture_size = 4096;
inline constexpr std::uint32_t identity_cache_capacity = 32;

enum class inspected_protocol : std::uint32_t {
  none = 0,
  http1 = 1,
  http2 = 2,
};

enum class inspection_action : std::uint32_t {
  none = 0,
  permitted = 1,
  blocked = 2,
  failed = 3,
};

enum inspection_flags : std::uint32_t {
  request_transformed = 0x00000001,
  response_transformed = 0x00000002,
};

struct certificate_config {
  std::array<std::byte, certificate_thumbprint_size> sha1_thumbprint;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
};

struct proxy_info {
  std::uint32_t process_id;
  std::uint16_t port_v4;
  std::uint16_t port_v6;
  std::uint32_t credentials_ready;
  std::uint64_t accepted;
  std::uint64_t handshaken;
  std::uint64_t permitted;
  std::uint64_t blocked;
  std::uint64_t failed;
  std::uint64_t origin_connected;
  std::uint64_t origin_completed;
  std::uint64_t identity_requests;
  std::uint64_t identity_timeouts;
  /** Total ring overwrites; per-reader loss is reported by read result. */
  std::uint64_t capture_overwritten;
  std::uint32_t identity_count;
  std::uint32_t identity_capacity;
};

struct inspection_record {
  std::uint64_t sequence;
  std::uint64_t session_id;
  std::uint16_t original_family;
  std::uint16_t original_port;
  std::array<std::byte, 16> original_address;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
  inspected_protocol protocol;
  inspection_action action;
  std::uint32_t flags;
  std::uint32_t status;
  std::int32_t failure_status;
  std::uint32_t request_size;
  std::uint32_t response_size;
  std::array<std::byte, maximum_capture_size> request;
  std::array<std::byte, maximum_capture_size> response;
};

struct inspection_cursor {
  std::uint64_t after_sequence;
};

struct inspection_read_result {
  std::uint32_t available;
  std::uint32_t dropped;
  std::uint64_t oldest_sequence;
  std::uint64_t current_sequence;
  inspection_record record;
};

struct identity_request {
  std::uint64_t sequence;
  std::uint64_t session_id;
  std::uint32_t server_name_size;
  std::array<char, maximum_server_name_size + 1> server_name;
};

struct identity_request_read_result {
  std::uint32_t available;
  std::uint32_t dropped;
  std::uint64_t oldest_sequence;
  std::uint64_t current_sequence;
  identity_request request;
};

struct configure_certificate_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96b;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_WRITE_DATA;
  using input_type = certificate_config;
  using output_type = void;
};
struct query_proxy_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96c;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = proxy_info;
};
struct query_last_inspection_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96d;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = void;
  using output_type = inspection_record;
};
struct read_inspection_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96e;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = inspection_cursor;
  using output_type = inspection_read_result;
};
struct read_identity_request_contract {
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x96f;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  using input_type = inspection_cursor;
  using output_type = identity_request_read_result;
};
inline constexpr unsigned long configure_certificate_ioctl =
    CTL_CODE(configure_certificate_contract::device_type,
             configure_certificate_contract::function,
             configure_certificate_contract::method,
             configure_certificate_contract::access);
inline constexpr unsigned long query_proxy_ioctl =
    CTL_CODE(query_proxy_contract::device_type, query_proxy_contract::function,
             query_proxy_contract::method, query_proxy_contract::access);
inline constexpr unsigned long query_last_inspection_ioctl =
    CTL_CODE(query_last_inspection_contract::device_type,
             query_last_inspection_contract::function,
             query_last_inspection_contract::method,
             query_last_inspection_contract::access);
inline constexpr unsigned long read_inspection_ioctl =
    CTL_CODE(read_inspection_contract::device_type,
             read_inspection_contract::function,
             read_inspection_contract::method,
             read_inspection_contract::access);
inline constexpr unsigned long read_identity_request_ioctl =
    CTL_CODE(read_identity_request_contract::device_type,
             read_identity_request_contract::function,
             read_identity_request_contract::method,
             read_identity_request_contract::access);

// {439DC1E6-DDE0-4459-845A-A42321EA1B0F}
inline constexpr GUID device_class_guid{
    0x439dc1e6, 0xdde0, 0x4459,
    {0x84, 0x5a, 0xa4, 0x23, 0x21, 0xea, 0x1b, 0x0f}};
// {4F8CD0EA-02B1-4D87-B9C3-D8AB3A6D89C5}
inline constexpr ntl::wfp::provider_key provider_key{
    GUID{0x4f8cd0ea, 0x02b1, 0x4d87,
         {0xb9, 0xc3, 0xd8, 0xab, 0x3a, 0x6d, 0x89, 0xc5}}};
// {8B47446C-8446-414A-ADB6-1BAE62D6DFD0}
inline constexpr ntl::wfp::sublayer_key sublayer_key{
    GUID{0x8b47446c, 0x8446, 0x414a,
         {0xad, 0xb6, 0x1b, 0xae, 0x62, 0xd6, 0xdf, 0xd0}}};
// {27605D67-B8FA-4F48-8F04-2B4E7679DD5D}
inline constexpr ntl::wfp::terminating_callout_key<layer_v4> callout_key_v4{
    GUID{0x27605d67, 0xb8fa, 0x4f48,
         {0x8f, 0x04, 0x2b, 0x4e, 0x76, 0x79, 0xdd, 0x5d}}};
// {47AA9E25-B1F4-419F-96F3-3315D9403201}
inline constexpr ntl::wfp::filter_key<layer_v4> filter_key_v4{
    GUID{0x47aa9e25, 0xb1f4, 0x419f,
         {0x96, 0xf3, 0x33, 0x15, 0xd9, 0x40, 0x32, 0x01}}};
// {36503CD3-09EC-4F61-9482-B7B260FC3DFA}
inline constexpr ntl::wfp::terminating_callout_key<layer_v6> callout_key_v6{
    GUID{0x36503cd3, 0x09ec, 0x4f61,
         {0x94, 0x82, 0xb7, 0xb2, 0x60, 0xfc, 0x3d, 0xfa}}};
// {85D6346F-11DB-4961-94D0-FD08B3F33DE4}
inline constexpr ntl::wfp::filter_key<layer_v6> filter_key_v6{
    GUID{0x85d6346f, 0x11db, 0x4961,
         {0x94, 0xd0, 0xfd, 0x08, 0xb3, 0xf3, 0x3d, 0xe4}}};

} // namespace wfp_kernel_tls_inspection_proxy
