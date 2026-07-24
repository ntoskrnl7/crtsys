#pragma once

#include <cstdint>

#if !defined(CTL_CODE) || !defined(GUID_DEFINED)
#error Include <ntddk.h> in kernel mode or <windows.h> and <winioctl.h> in user mode before this header.
#endif

namespace kmdf_usb_ntl_sample {

inline constexpr GUID device_interface_guid = {
    0x9b2aad09,
    0xf960,
    0x46bd,
    {0x99, 0x34, 0xe3, 0x83, 0xef, 0x8b, 0xd3, 0x72}};

struct query_reply {
  std::uint16_t vendor_id;
  std::uint16_t product_id;
  std::uint8_t interface_count;
  std::uint8_t configured_pipe_count;
  std::uint8_t input_endpoint;
  std::uint8_t output_endpoint;
  std::uint32_t input_packet_size;
  std::uint32_t output_packet_size;
  std::uint32_t continuous_read_count;
};

struct query_ioctl_contract {
  using output_type = query_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9b4;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long query_ioctl_code = query_ioctl_contract::code;

} // namespace kmdf_usb_ntl_sample
