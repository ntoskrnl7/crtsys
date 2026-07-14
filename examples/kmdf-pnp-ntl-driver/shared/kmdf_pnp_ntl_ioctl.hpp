#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_pnp_ntl_sample {

inline constexpr GUID device_interface_guid = {
    0xb7e550bc,
    0xf87a,
    0x4f91,
    {0xa7, 0xdb, 0xb3, 0x8c, 0xe1, 0x4d, 0x82, 0xb4}};

inline constexpr wchar_t hardware_id[] =
    L"Root\\CrtSysKmdfNtlPnpSample";

struct query_request {
  std::uint32_t value;
};

struct query_reply {
  std::uint32_t value;
  std::uint32_t io_count;
  std::uint32_t prepare_count;
  std::uint32_t d0_entry_count;
  std::uint32_t server_irql;
  char message[96];
};

struct query_ioctl_contract {
  using input_type = query_request;
  using output_type = query_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b2;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long query_ioctl_code = query_ioctl_contract::code;

} // namespace kmdf_pnp_ntl_sample
