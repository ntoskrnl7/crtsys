#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace ntl_net_kernel_sample {

inline constexpr wchar_t device_name[] = L"CrtSysNtlNetKernelContracts";
inline constexpr std::size_t maximum_wire_size = 2048;
inline constexpr std::uint32_t expected_msquic_api_version = 2;

enum class protocol : std::uint32_t {
  grpc = 1,
  websocket = 2,
  qpack = 3,
  tls_client_hello = 4,
  transform = 5,
  executor = 6,
  http1 = 7,
  http2 = 8,
  http3 = 9,
  webtransport = 10,
  offload_contract = 11,
  codec_gzip = 12,
  codec_brotli = 13,
  wsk_tcp = 14,
  x509_issue = 15,
  schannel_client = 16,
  wsk_tls = 17,
  msquic_nmr = 18,
  qpack_dynamic = 19,
  webtransport_backend = 20,
  http_transform = 21,
  wsk_listener = 22,
  async_stream_state_machine = 23,
  workspace_lifetime = 24,
  wfp_injection_lifetime = 25,
  waitable_task_lifetime = 26,
  udp_mapping_lifetime = 27,
  bounded_wait_set = 28,
  executor_lifetime = 29,
  http3_origin_pool_lifetime = 30,
};

struct inspect_request {
  protocol kind = protocol::grpc;
  std::uint32_t size = 0;
  std::array<std::byte, maximum_wire_size> wire{};
};

struct inspect_reply {
  NTSTATUS parse_status = STATUS_UNSUCCESSFUL;
  std::uint32_t content_size = 0;
  std::uint32_t field_count = 0;
  std::uint32_t flags = 0;
  std::array<std::byte, maximum_wire_size> transformed{};
};

struct inspect_ioctl_contract {
  using input_type = inspect_request;
  using output_type = inspect_reply;
  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9A2;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long inspect_ioctl_code =
    inspect_ioctl_contract::code;

namespace result_flag {
inline constexpr std::uint32_t compressed = 1u << 0;
inline constexpr std::uint32_t masked = 1u << 1;
inline constexpr std::uint32_t server_name = 1u << 2;
inline constexpr std::uint32_t ech = 1u << 3;
inline constexpr std::uint32_t transformed = 1u << 4;
inline constexpr std::uint32_t offloaded = 1u << 5;
inline constexpr std::uint32_t codec_round_trip = 1u << 6;
inline constexpr std::uint32_t wsk_round_trip = 1u << 7;
inline constexpr std::uint32_t x509_generated = 1u << 8;
inline constexpr std::uint32_t schannel_client_hello = 1u << 9;
inline constexpr std::uint32_t tls_round_trip = 1u << 10;
inline constexpr std::uint32_t msquic_nmr_bound = 1u << 11;
inline constexpr std::uint32_t qpack_resumed = 1u << 12;
inline constexpr std::uint32_t webtransport_session = 1u << 13;
inline constexpr std::uint32_t http_all_versions = 1u << 14;
inline constexpr std::uint32_t wsk_listener_round_trip = 1u << 15;
inline constexpr std::uint32_t async_stream_serialized = 1u << 16;
inline constexpr std::uint32_t credential_passive_cleanup = 1u << 17;
inline constexpr std::uint32_t workspace_fail_closed = 1u << 18;
inline constexpr std::uint32_t workspace_passive_cleanup = 1u << 19;
inline constexpr std::uint32_t injection_passive_cleanup = 1u << 20;
inline constexpr std::uint32_t task_passive_cleanup = 1u << 21;
inline constexpr std::uint32_t udp_mapping_fail_closed = 1u << 22;
inline constexpr std::uint32_t bounded_wait_blocks = 1u << 23;
inline constexpr std::uint32_t executor_lifetime_safe = 1u << 24;
inline constexpr std::uint32_t http3_origin_pool_lifetime_safe = 1u << 25;
inline constexpr std::uint32_t http2_resume_stack_safe = 1u << 26;
} // namespace result_flag

} // namespace ntl_net_kernel_sample
