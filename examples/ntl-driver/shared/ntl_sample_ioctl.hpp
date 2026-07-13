#pragma once

#include <cstdint>

#ifndef CTL_CODE
#error Include <wdm.h> in kernel mode or <winioctl.h> in user mode before this header.
#endif

namespace ntl_sample {

inline constexpr wchar_t device_name[] = L"CrtSysNtlSample";

struct ping_request {
  std::uint32_t value;
};

struct ping_reply {
  std::uint32_t value;
  std::uint32_t sequence;
  std::uint32_t configured_flags;
  std::uint32_t checksum;
};

struct ping_ioctl_contract {
  using input_type = ping_request;
  using output_type = ping_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9A1;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access = FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long ping_ioctl_code =
    ping_ioctl_contract::code;

} // namespace ntl_sample
