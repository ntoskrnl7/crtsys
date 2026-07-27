#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_filter_stack_ntl_sample {

inline constexpr GUID device_interface_guid = {
    0xc5d0b96a,
    0xe1b6,
    0x48fa,
    {0x9e, 0x26, 0x27, 0x74, 0x4b, 0xe7, 0x2e, 0xdd}};

inline constexpr wchar_t hardware_id[] =
    L"Root\\CrtSysKmdfNtlFilterStackSample";

enum layer : std::uint32_t {
  target_layer = 0x1,
  filter_layer = 0x2,
};

struct query_request {
  std::uint32_t value;
};

struct query_reply {
  std::uint32_t value;
  std::uint32_t layers;
  std::uint32_t target_requests;
  std::uint32_t filter_completions;
  std::uint32_t prepare_count;
  std::uint32_t d0_count;
  std::uint32_t server_irql;
  char message[64];
};

static_assert(sizeof(query_request) == 4);
static_assert(sizeof(query_reply) == 92);

struct query_ioctl_contract {
  using input_type = query_request;
  using output_type = query_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b7;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long query_ioctl_code = query_ioctl_contract::code;

} // namespace kmdf_filter_stack_ntl_sample
