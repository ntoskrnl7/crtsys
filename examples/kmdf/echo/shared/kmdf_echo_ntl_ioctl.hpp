#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_echo_ntl_sample {

inline constexpr GUID device_interface_guid = {
    0xdc547e1f,
    0x40cd,
    0x45fb,
    {0xa2, 0x62, 0x3f, 0x10, 0x38, 0xcf, 0x9e, 0x06}};

inline constexpr wchar_t hardware_id[] = L"Root\\CrtSysKmdfNtlEchoSample";

struct echo_request {
  std::uint32_t value;
  std::uint32_t delay_ms;
};

struct echo_reply {
  std::uint32_t value;
  std::uint32_t delay_ms;
  std::uint32_t completed_requests;
  std::uint32_t canceled_requests;
  std::uint32_t server_irql;
  char message[64];
};

static_assert(sizeof(echo_request) == 8);
static_assert(sizeof(echo_reply) == 84);

struct echo_ioctl_contract {
  using input_type = echo_request;
  using output_type = echo_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b6;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long echo_ioctl_code = echo_ioctl_contract::code;

} // namespace kmdf_echo_ntl_sample
