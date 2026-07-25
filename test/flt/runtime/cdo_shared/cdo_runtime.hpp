#pragma once

#include <cstdint>

#ifndef CTL_CODE
#error Include <ntl/flt/all> in kernel mode or <winioctl.h> in user mode first.
#endif

namespace crtsys_flt_cdo_runtime {

inline constexpr wchar_t filter_name[] = L"CrtSysFltCdoRuntimeTest";
inline constexpr wchar_t device_name[] = L"CrtSysFltCdoRuntime";
inline constexpr wchar_t user_device_path[] =
    LR"(\\.\CrtSysFltCdoRuntime)";

struct ping_request {
  std::uint32_t value = 0;
};

struct ping_reply {
  std::uint32_t value = 0;
  std::uint32_t sequence = 0;
  std::uint32_t create_count = 0;
  std::uint32_t ioctl_count = 0;
  std::uint32_t unload_veto_count = 0;
};

struct ping_ioctl_contract {
  using input_type = ping_request;
  using output_type = ping_reply;

  static constexpr unsigned long device_type = FILE_DEVICE_UNKNOWN;
  static constexpr unsigned long function = 0x9B0;
  static constexpr unsigned long method = METHOD_BUFFERED;
  static constexpr unsigned long access =
      FILE_READ_DATA | FILE_WRITE_DATA;
  static constexpr unsigned long code =
      CTL_CODE(device_type, function, method, access);
};

inline constexpr unsigned long ping_ioctl = ping_ioctl_contract::code;

} // namespace crtsys_flt_cdo_runtime
